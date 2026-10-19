#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QWindow;

// Claude-style computer use for Buddy: action parsing, coordinate scaling
// between the model's screenshot space and real (DPI-scaled, multi-monitor)
// screen coordinates, a rate limiter, an input backend (SendInput on
// Windows, a recording simulator elsewhere and in tests) and the controller
// that drives the on-screen agent cursor, edge glow and control HUD.
namespace cu {

enum class Action {
    Invalid,
    Screenshot, Zoom, CursorPosition, Wait,                    // observe
    MouseMove, LeftClick, RightClick, MiddleClick, DoubleClick, TripleClick,
    LeftClickDrag, Scroll, Type, Key,                           // input
};

struct KeyCombo {
    QStringList modifiers;   // canonical: ctrl, alt, shift, super
    QString key;             // canonical key name: a, f4, return, escape, ...
    QString error;
    bool valid() const { return error.isEmpty(); }
    QString toString() const;
};

struct ParsedAction {
    Action action = Action::Invalid;
    QString name;
    bool hasCoord = false;
    QPoint coord;            // agent (screenshot) space
    bool hasStart = false;
    QPoint start;            // left_click_drag start, agent space
    QString text;            // type
    KeyCombo combo;          // key
    QString scrollDirection; // up / down / left / right
    int scrollAmount = 3;
    double duration = 1.0;   // wait, seconds
    QRect region;            // zoom, agent space
    int display = -1;        // screenshot: switch to this display first
    QString error;
    bool valid() const { return error.isEmpty() && action != Action::Invalid; }
    bool isInput() const;
};

constexpr int kMaxTypeChars = 2000;
constexpr int kMaxScrollAmount = 25;

QStringList actionNames();
bool isInputAction(Action a);
ParsedAction parseAction(const QJsonObject &input, const QSize &agentSize);
KeyCombo parseKeyCombo(const QString &combo);
// Combos that open a run dialog, a terminal, task manager, lock or sign out:
// fine for the user, but asked for even in Trusted mode once the agent has
// read the screen (which may be showing it instructions).
bool isRiskyKeyCombo(const KeyCombo &combo);
// Typed text that reads like a shell command or URL fetch.
bool looksLikeShellText(const QString &text);

} // namespace cu

// One monitor. geometry is Qt's logical (device-independent) global rect;
// nativeOrigin is the same monitor's top-left in physical pixels, which is
// what SendInput / SetCursorPos use in a per-monitor DPI aware process.
struct ScreenInfo {
    QRect geometry;
    qreal dpr = 1.0;
    QPoint nativeOrigin;
    QString name;
    bool primary = false;
};

class CoordinateMapper
{
public:
    // Largest size inside maxSize with the screen's aspect ratio; never
    // upscales (Anthropic recommends <= 1280x800 for computer use).
    static QSize agentSizeFor(const QSize &logical, const QSize &maxSize = QSize(1280, 800));
    static QList<ScreenInfo> qtScreens();

    void setScreens(const QList<ScreenInfo> &screens);
    QList<ScreenInfo> screens() const { return m_screens; }
    bool setDisplay(int index);   // false if out of range
    int display() const { return m_display; }
    ScreenInfo current() const;
    QSize agentSize() const;

    QPoint agentToLogical(const QPoint &agent) const;   // clamped to the display
    QPoint logicalToAgent(const QPoint &logical) const; // clamped to agent space
    QRect agentRectToLogical(const QRect &agent) const;
    QPoint logicalToPhysical(const QPoint &logical) const;   // via the monitor containing it
    QPoint physicalToLogical(const QPoint &physical) const;
    int screenAt(const QPoint &logical) const;

private:
    QList<ScreenInfo> m_screens;
    int m_display = 0;
};

// At most `burst` input actions per `windowMs`, and at least `minGapMs`
// between two of them.
class RateLimiter
{
public:
    RateLimiter(int burst = 40, qint64 windowMs = 60000, qint64 minGapMs = 80)
        : m_burst(burst), m_windowMs(windowMs), m_minGapMs(minGapMs) {}
    bool tryAcquire(qint64 nowMs, qint64 *retryInMs = nullptr);
    void reset() { m_stamps.clear(); }
    int burst() const { return m_burst; }
    qint64 windowMs() const { return m_windowMs; }

private:
    int m_burst;
    qint64 m_windowMs;
    qint64 m_minGapMs;
    QList<qint64> m_stamps;
};

class ComputerBackend
{
public:
    virtual ~ComputerBackend() = default;
    virtual QString name() const = 0;
    virtual QPoint cursorPos() const = 0;                 // logical global
    virtual void moveTo(const QPoint &logical) = 0;
    virtual void button(Qt::MouseButton b, bool down) = 0;
    virtual void scroll(int dx, int dy) = 0;              // wheel notches; +dy = up
    virtual bool typeText(const QString &text) = 0;
    virtual bool keyCombo(const cu::KeyCombo &combo) = 0;
    virtual QImage grab(const ScreenInfo &screen) = 0;    // physical-resolution image of one monitor
    virtual bool focusedIsPassword() const = 0;
    // Watch for real (non-injected) user input while the agent is in control.
    virtual void setWatching(bool on) { Q_UNUSED(on); }
    std::function<void(bool abort)> onUserInput;          // abort: Esc held / Ctrl+Alt+Esc
    void setMapper(const CoordinateMapper *m) { m_mapper = m; }

protected:
    const CoordinateMapper *m_mapper = nullptr;
};

// Records what it would do. Used off Windows (so the app builds and runs)
// and in tests. Screenshots come from QScreen when there is one, else a
// generated test card.
class SimulatedBackend : public ComputerBackend
{
public:
    QString name() const override { return QStringLiteral("simulated"); }
    QPoint cursorPos() const override { return m_cursor; }
    void moveTo(const QPoint &p) override;
    void button(Qt::MouseButton b, bool down) override;
    void scroll(int dx, int dy) override;
    bool typeText(const QString &text) override;
    bool keyCombo(const cu::KeyCombo &combo) override;
    QImage grab(const ScreenInfo &screen) override;
    bool focusedIsPassword() const override { return passwordFocused; }

    QStringList log;
    bool passwordFocused = false;
    bool useRealScreens = true;

private:
    QPoint m_cursor;
};

#ifdef Q_OS_WIN
std::unique_ptr<ComputerBackend> makeWindowsBackend();
#endif
std::unique_ptr<ComputerBackend> makeDefaultBackend();

class ComputerUse : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY stateChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY stateChanged)
    Q_PROPERTY(QString actionKind READ actionKind NOTIFY actionChanged)
    Q_PROPERTY(QString actionLabel READ actionLabel NOTIFY actionChanged)
    Q_PROPERTY(QPointF cursor READ cursor NOTIFY cursorChanged)
    Q_PROPERTY(int moveMs READ moveMs NOTIFY cursorChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY actionChanged)
    Q_PROPERTY(QPointF dragFrom READ dragFrom NOTIFY actionChanged)
    Q_PROPERTY(bool typing READ typing NOTIFY actionChanged)
    Q_PROPERTY(int clickSerial READ clickSerial NOTIFY clicked)
    Q_PROPERTY(QRect displayGeometry READ displayGeometry NOTIFY displayChanged)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    // Every connected screen (QScreen objects), so the overlay can put a glow
    // window on each. Updates when screens are plugged in or removed.
    Q_PROPERTY(QVariantList screens READ qmlScreens NOTIFY screensChanged)
    Q_PROPERTY(bool showCursor READ showCursor WRITE setShowCursor NOTIFY settingsChanged)
    Q_PROPERTY(bool showGlow READ showGlow WRITE setShowGlow NOTIFY settingsChanged)
    Q_PROPERTY(qreal glowIntensity READ glowIntensity WRITE setGlowIntensity NOTIFY settingsChanged)

public:
    struct Result {
        QString text;
        QString imageBase64;   // JPEG for the model (screenshot / zoom)
        QString thumbnail;     // data: URL for the trace UI
        bool isError = false;
    };
    using Done = std::function<void(const Result &)>;

    explicit ComputerUse(QObject *parent = nullptr);
    ~ComputerUse() override;

    void setBackend(std::unique_ptr<ComputerBackend> backend);
    ComputerBackend *backend() const { return m_backend.get(); }
    CoordinateMapper &mapper() { return m_mapper; }
    const CoordinateMapper &mapper() const { return m_mapper; }
    // Read the monitor layout from Qt before each action (off in tests).
    void setAutoScreens(bool on) { m_autoScreens = on; }
    void refreshScreens();
    // 1 = real animation timing; 0 = no waits (tests).
    void setAnimationScale(double s) { m_animScale = qMax(0.0, s); }
    RateLimiter &rateLimiter() { return m_limiter; }

    bool active() const { return m_active; }
    bool paused() const { return m_paused; }
    QString pauseReason() const { return m_pauseReason; }
    bool capturing() const { return m_capturing; }
    QString actionKind() const { return m_actionKind; }
    QString actionLabel() const { return m_actionLabel; }
    QPointF cursor() const { return m_cursor; }
    int moveMs() const { return m_moveMs; }
    bool dragging() const { return m_dragging; }
    QPointF dragFrom() const { return m_dragFrom; }
    bool typing() const { return m_typing; }
    int clickSerial() const { return m_clickSerial; }
    QRect displayGeometry() const { return m_mapper.current().geometry; }
    QVariantList qmlScreens() const;
    QString backendName() const;
    bool showCursor() const { return m_showCursor; }
    void setShowCursor(bool on);
    bool showGlow() const { return m_showGlow; }
    void setShowGlow(bool on);
    qreal glowIntensity() const { return m_glowIntensity; }
    void setGlowIntensity(qreal v);

    // Agent session: begin() when the first action of a run starts, end()
    // when the run stops. end() drops anything still in flight.
    void begin();
    void end();
    void perform(const cu::ParsedAction &a, Done done);

    // Real user input arrived while the agent was in control.
    void userInput(bool abort);

    Q_INVOKABLE void stop();     // HUD Stop button / hotkey
    Q_INVOKABLE void resume();
    Q_INVOKABLE void pause(const QString &reason = QString());
    // Keep an overlay window out of screenshots (Windows 10 2004+).
    Q_INVOKABLE void excludeFromCapture(QWindow *window);
    // Limit where a window takes input (and paints) to one rectangle, so the
    // HUD window can stay a fixed size without blocking clicks around the pill.
    Q_INVOKABLE void setInputRect(QWindow *window, const QRect &rect);

    static QString describe(const cu::ParsedAction &a);
    QImage captureDisplay();   // current display, physical resolution, overlays hidden

signals:
    void stateChanged();
    void actionChanged();
    void cursorChanged();
    void clicked();
    void displayChanged();
    void settingsChanged();
    void screensChanged();
    void stopRequested(const QString &reason);

private:
    void setAction(const QString &kind, const QString &label);
    void animateTo(const QPoint &logical, std::function<void()> then);
    void later(int ms, std::function<void()> fn);
    QString thumbnailFor(const QImage &img, const QPointF &markLogical = QPointF(-1, -1)) const;
    static QString jpegBase64(const QImage &img, int quality);
    void finishInput(const cu::ParsedAction &a, const QPoint &target, Done done);

    std::unique_ptr<ComputerBackend> m_backend;
    CoordinateMapper m_mapper;
    RateLimiter m_limiter;
    QElapsedTimer m_clock;
    quint64 m_generation = 0;
    bool m_autoScreens = true;
    double m_animScale = 1.0;
    bool m_active = false;
    bool m_paused = false;
    QString m_pauseReason;
    bool m_capturing = false;
    QString m_actionKind;
    QString m_actionLabel;
    QPointF m_cursor;
    int m_moveMs = 0;
    bool m_dragging = false;
    QPointF m_dragFrom;
    bool m_typing = false;
    int m_clickSerial = 0;
    bool m_showCursor = true;
    bool m_showGlow = true;
    qreal m_glowIntensity = 0.6;
};
