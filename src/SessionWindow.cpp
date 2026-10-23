#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "SessionWindow.h"
#include "SystemUtils.h"

#include <algorithm>
#include <chrono>

void LockKiosk();

namespace
{
constexpr int kButtonWindowSize = 68;
constexpr int kButtonSize = 60;
constexpr int kScreenMargin = 20;
constexpr int kPopupWidth = 330;
constexpr int kPopupSpacing = 12;

class MouseToggleButton final : public QPushButton
{
public:
    explicit MouseToggleButton(QWidget* parent = nullptr)
        : QPushButton(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPushButton::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(255, 255, 255), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        const qreal w = width();
        const qreal h = height();
        const QRectF mouseBody(w * 0.32, h * 0.18, w * 0.36, h * 0.64);
        painter.drawRoundedRect(mouseBody, mouseBody.width() * 0.48, mouseBody.width() * 0.48);
        painter.drawLine(
            QPointF(mouseBody.center().x(), mouseBody.top() + 2.0),
            QPointF(mouseBody.center().x(), mouseBody.top() + h * 0.20));
    }
};

class SessionActive final
{
public:
    SessionActive()
        : m_sessionStart(std::chrono::steady_clock::now())
    {
        createWidgets();
    }

    ~SessionActive()
    {
        if (m_timer != nullptr)
            m_timer->stop();

        // The popup is owned by the floating button window.
        delete m_buttonWindow;
        m_buttonWindow = nullptr;
        m_popup = nullptr;
        m_timer = nullptr;
    }

    bool isValid() const
    {
        return m_buttonWindow != nullptr
            && m_toggleButton != nullptr
            && m_popup != nullptr
            && m_timeLabel != nullptr;
    }

    void updateTime()
    {
        if (m_timeLabel == nullptr)
            return;

        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_sessionStart).count();

        const qint64 hours = static_cast<qint64>(elapsed / 3600);
        const qint64 minutes = static_cast<qint64>((elapsed % 3600) / 60);
        const qint64 seconds = static_cast<qint64>(elapsed % 60);

        m_timeLabel->setText(
            QStringLiteral("%1:%2:%3")
                .arg(hours, 2, 10, QChar(u'0'))
                .arg(minutes, 2, 10, QChar(u'0'))
                .arg(seconds, 2, 10, QChar(u'0')));
    }

private:
    void createWidgets()
    {
        m_buttonWindow = new QWidget(
            nullptr,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_buttonWindow->setObjectName(QStringLiteral("sessionToggleWindow"));
        m_buttonWindow->setWindowTitle(QStringLiteral("Información de sesión"));
        m_buttonWindow->setAttribute(Qt::WA_TranslucentBackground);
        m_buttonWindow->setAttribute(Qt::WA_ShowWithoutActivating);
        m_buttonWindow->setFixedSize(kButtonWindowSize, kButtonWindowSize);

        auto* buttonLayout = new QVBoxLayout(m_buttonWindow);
        buttonLayout->setContentsMargins(4, 4, 4, 4);
        buttonLayout->setSpacing(0);

        m_toggleButton = new MouseToggleButton(m_buttonWindow);
        m_toggleButton->setObjectName(QStringLiteral("sessionToggleButton"));
        m_toggleButton->setAccessibleName(QStringLiteral("Mostrar información de sesión"));
        m_toggleButton->setToolTip(QStringLiteral("Información de sesión"));
        m_toggleButton->setCursor(Qt::PointingHandCursor);
        m_toggleButton->setFocusPolicy(Qt::NoFocus);
        m_toggleButton->setFixedSize(kButtonSize, kButtonSize);
        m_toggleButton->setStyleSheet(QStringLiteral(
            "QPushButton#sessionToggleButton {"
            " background-color: #2563eb; color: #ffffff;"
            " border: 2px solid #ffffff; border-radius: 30px;"
            " font-family: 'Segoe UI Symbol'; font-size: 30px; padding: 0;"
            "}"
            "QPushButton#sessionToggleButton:hover { background-color: #3b82f6; }"
            "QPushButton#sessionToggleButton:pressed { background-color: #1d4ed8; }"));
        buttonLayout->addWidget(m_toggleButton, 0, Qt::AlignCenter);

        // Qt::Popup provides the required outside-click and Escape dismissal.
        m_popup = new QFrame(m_buttonWindow, Qt::Popup | Qt::FramelessWindowHint);
        m_popup->setObjectName(QStringLiteral("sessionPopup"));
        m_popup->setWindowTitle(QStringLiteral("Sesión activa"));
        m_popup->setAttribute(Qt::WA_TranslucentBackground); 
        m_popup->setFocusPolicy(Qt::StrongFocus);
        m_popup->setFixedWidth(kPopupWidth);

        auto* container = new QFrame(m_popup);
        container->setObjectName(QStringLiteral("popupContainer"));

        m_popup->setStyleSheet(QStringLiteral(
            "QFrame#popupContainer {"
            " background-color: #181d28; color: #e5e7eb;"
            " border: 1px solid #374151; border-radius: 16px;"
            "}"
            "QLabel { background: transparent; border: none; color: #e5e7eb; }"
            "QLabel#sessionTitle { color: #ffffff; font-size: 20px; font-weight: 700; }"
            "QLabel#sessionSection { color: #93c5fd; font-size: 10px; font-weight: 700; }"
            "QLabel#sessionComputer { color: #ffffff; font-size: 20px; font-weight: 700; }"
            "QLabel#sessionTime { color: #60a5fa; font-family: Consolas; font-size: 30px; font-weight: 700; }"
            "QLabel#sessionDetail { color: #cbd5e1; font-size: 12px; }"
            "QPushButton#closeSessionButton {"
            " background-color: #2563eb; color: #ffffff; border: none;"
            " border-radius: 8px; padding: 10px 14px; font-size: 14px; font-weight: 600;"
            "}"
            "QPushButton#closeSessionButton:hover { background-color: #3b82f6; }"
            "QPushButton#closeSessionButton:pressed { background-color: #1d4ed8; }"));

        // El layout principal del popup SOLO contiene al contenedor interno
        auto* mainLayout = new QVBoxLayout(m_popup);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(container);

        auto* popupLayout = new QVBoxLayout(container);
        popupLayout->setContentsMargins(22, 20, 22, 20);
        popupLayout->setSpacing(10);

        auto* title = new QLabel(QStringLiteral("SESIÓN ACTIVA"), m_popup);
        title->setObjectName(QStringLiteral("sessionTitle"));
        popupLayout->addWidget(title);
        title->hide();

        auto* section = new QLabel(QStringLiteral("IDENTIFICADOR DE EQUIPO"), m_popup);
        section->setObjectName(QStringLiteral("sessionSection"));
        popupLayout->addWidget(section);
        section->hide();

        auto* computerName = new QLabel(
            QString::fromStdWString(GetComputerNameString()), m_popup);
        computerName->setObjectName(QStringLiteral("sessionComputer"));
        computerName->setAlignment(Qt::AlignCenter);
        computerName->setWordWrap(true);
        computerName->setTextInteractionFlags(Qt::TextSelectableByMouse);
        popupLayout->addWidget(computerName);

        auto* divider = new QFrame(m_popup);
        divider->setFrameShape(QFrame::HLine);
        divider->setFixedHeight(1);
        divider->setStyleSheet(QStringLiteral(
            "QFrame { background-color: #374151; border: none; }"));
        popupLayout->addWidget(divider);
        divider->hide();

        m_timeLabel = new QLabel(QStringLiteral("00:00:00"), m_popup);
        m_timeLabel->setObjectName(QStringLiteral("sessionTime"));
        m_timeLabel->setAlignment(Qt::AlignCenter);
        m_timeLabel->setMinimumHeight(42);
        popupLayout->addWidget(m_timeLabel);

        auto* userLabel = new QLabel(
            QStringLiteral("Usuario: %1").arg(QString::fromStdWString(GetUserNameString())),
            m_popup);
        userLabel->setObjectName(QStringLiteral("sessionDetail"));
        userLabel->setWordWrap(true);
        popupLayout->addWidget(userLabel);
        userLabel->hide();

        auto* ipLabel = new QLabel(
            QStringLiteral("IP: %1").arg(QString::fromStdWString(GetIpAddress())),
            m_popup);
        ipLabel->setObjectName(QStringLiteral("sessionDetail"));
        ipLabel->setWordWrap(true);
        popupLayout->addWidget(ipLabel);
        ipLabel->hide();

        auto* closeButton = new QPushButton(QStringLiteral("Cerrar sesión"), m_popup);
        closeButton->setObjectName(QStringLiteral("closeSessionButton"));
        closeButton->setCursor(Qt::PointingHandCursor);
        closeButton->setFocusPolicy(Qt::StrongFocus);
        popupLayout->addWidget(closeButton);

        QObject::connect(m_toggleButton, &QPushButton::clicked, m_buttonWindow, [this]()
        {
            if (m_popup == nullptr)
                return;

            if (m_popup->isVisible())
            {
                m_popup->hide();
                return;
            }

            updateTime();
            positionWindows();
            m_popup->show();
            m_popup->raise();
            m_popup->activateWindow();
            m_popup->setFocus(Qt::PopupFocusReason);
        });

        QObject::connect(closeButton, &QPushButton::clicked, m_popup, []()
        {
            // Defer destruction until the clicked signal has finished dispatching.
            QTimer::singleShot(0, qApp, []()
            {
                LockKiosk();
            });
        });

        m_timer = new QTimer(m_buttonWindow);
        m_timer->setInterval(1000);
        QObject::connect(m_timer, &QTimer::timeout, m_buttonWindow, [this]()
        {
            updateTime();
        });
        m_timer->start();

        if (QScreen* screen = QGuiApplication::primaryScreen())
        {
            QObject::connect(screen, &QScreen::availableGeometryChanged,
                    m_buttonWindow, [this](const QRect&) { positionWindows(); });
            QObject::connect(screen, &QScreen::geometryChanged,
                    m_buttonWindow, [this](const QRect&) { positionWindows(); });
        }

        updateTime();
        positionWindows();
        m_buttonWindow->show();
        m_buttonWindow->raise();
    }

    void positionWindows()
    {
        if (m_buttonWindow == nullptr)
            return;

        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
            return;

        const QRect area = screen->availableGeometry();
        const int buttonX = area.x() + area.width() - kButtonWindowSize - kScreenMargin;
        const int buttonY = area.y() + area.height() - kButtonWindowSize - kScreenMargin;
        m_buttonWindow->move(buttonX, buttonY);

        if (m_popup == nullptr)
            return;

        m_popup->adjustSize();
        const int popupWidth = m_popup->width();
        const int popupHeight = m_popup->height();

        int popupX = buttonX + kButtonWindowSize - popupWidth;
        int popupY = buttonY - popupHeight - kPopupSpacing;

        // If the popup cannot fit above the button, place it below instead.
        if (popupY < area.y() + kScreenMargin)
            popupY = buttonY + kButtonWindowSize + kPopupSpacing;

        const int minX = area.x() + kScreenMargin;
        const int maxX = (std::max)(minX, area.x() + area.width() - popupWidth - kScreenMargin);
        const int minY = area.y() + kScreenMargin;
        const int maxY = (std::max)(minY, area.y() + area.height() - popupHeight - kScreenMargin);

        popupX = (std::clamp)(popupX, minX, maxX);
        popupY = (std::clamp)(popupY, minY, maxY);
        m_popup->move(popupX, popupY);
    }

    QWidget* m_buttonWindow = nullptr;
    QPushButton* m_toggleButton = nullptr;
    QFrame* m_popup = nullptr;
    QLabel* m_timeLabel = nullptr;
    QTimer* m_timer = nullptr;
    std::chrono::steady_clock::time_point m_sessionStart;
};

SessionActive* g_sessionActive = nullptr;
}

// Qt widget-based session indicator. QApplication must exist before this API is used.
bool RegisterSessionWindowClass(HINSTANCE hInstance)
{
    (void)hInstance;
    return qApp != nullptr;
}

void CreateSessionWindow(HINSTANCE hInstance)
{
    (void)hInstance;

    if (g_sessionActive != nullptr || qApp == nullptr)
        return;

    auto* session = new SessionActive();
    if (!session->isValid())
    {
        delete session;
        OutputDebugStringW(L"[SESSION] No se pudo crear la interfaz Qt de sesión.\n");
        return;
    }

    g_sessionActive = session;
}

void DestroySessionWindow()
{
    delete g_sessionActive;
    g_sessionActive = nullptr;
}

void UpdateSessionTime()
{
    if (g_sessionActive != nullptr)
        g_sessionActive->updateTime();
}
