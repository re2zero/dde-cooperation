#ifndef TYPE_DEFINES_H
#define TYPE_DEFINES_H

#if defined(_WIN32) || defined(_WIN64)
#    include <QMainWindow>
#    include <QApplication>
#    include <QDialog>
#    include <QPushButton>
#    include <QToolButton>
#    include <QLabel>
#    include <QLineEdit>
#    include <QCheckBox>
#    include <QPainter>
#    include <QMouseEvent>
typedef QMainWindow CooperationMainWindow;
typedef QDialog CooperationDialog;
typedef QToolButton CooperationIconButton;
typedef QDialog CooperationAbstractDialog;
typedef QPushButton CooperationSuggestButton;
typedef QToolButton CooperationFloatingEdit;
typedef QLabel CooperationSpinner;
typedef QLineEdit CooperationLineEdit;
typedef QLabel CooperationLabel;
class CooperationSwitchButton : public QPushButton
{
public:
    CooperationSwitchButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFixedSize(50, 24);
        setCheckable(true);
        setDefault(false);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        int x = 0;
        QColor color("#000000");
        if (isChecked()) {
            x = 20;
            color.setNamedColor("#0098FF");
        }
        QRectF rect(x, 0, 30, 24);
        painter.setBrush(QColor(qRgba(220, 220, 220, 128)));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(this->rect(), 8, 8);
        painter.setBrush(color);
        painter.drawRoundedRect(rect, 8, 8);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            event->ignore();
            return;
        }
        QPushButton::keyPressEvent(event);
    }
};
#else
#    include <DMainWindow>
#    include <DAbstractDialog>
#    include <DSwitchButton>
#    include <DSuggestButton>
#    include <DSearchEdit>
#    include <DDialog>
#    include <DSpinner>
#    include <DIconButton>
#    include <DFloatingButton>
#    include <DLineEdit>
#    include <DLabel>
typedef DTK_WIDGET_NAMESPACE::DDialog CooperationDialog;
typedef DTK_WIDGET_NAMESPACE::DSpinner CooperationSpinner;
typedef DTK_WIDGET_NAMESPACE::DMainWindow CooperationMainWindow;
typedef DTK_WIDGET_NAMESPACE::DAbstractDialog CooperationAbstractDialog;
typedef DTK_WIDGET_NAMESPACE::DSwitchButton CooperationSwitchButton;
typedef DTK_WIDGET_NAMESPACE::DSuggestButton CooperationSuggestButton;
typedef DTK_WIDGET_NAMESPACE::DSearchEdit CooperationSearchEdit;
typedef DTK_WIDGET_NAMESPACE::DIconButton CooperationIconButton;
typedef DTK_WIDGET_NAMESPACE::DFloatingButton CooperationFloatingEdit;
typedef DTK_WIDGET_NAMESPACE::DLineEdit CooperationLineEdit;
typedef DTK_WIDGET_NAMESPACE::DLabel CooperationLabel;
#endif

namespace OperationKey {
inline constexpr char kID[] { "id" };
inline constexpr char kIconName[] { "icon-name" };
inline constexpr char kButtonStyle[] { "button-style" };
inline constexpr char kLocation[] { "location" };
inline constexpr char kDescription[] { "description" };
inline constexpr char kClickedCallback[] { "clicked-callback" };
inline constexpr char kVisibleCallback[] { "visible-callback" };
inline constexpr char kClickableCallback[] { "clickable-callback" };
}

namespace AppSettings {
inline constexpr char IPAddress[] { "IPAddress" };
inline constexpr char OSType[] { "osType" };
inline constexpr char GenericGroup[] { "GenericAttribute" };
inline constexpr char DeviceNameKey[] { "DeviceName" };
inline constexpr char DiscoveryModeKey[] { "DiscoveryMode" };
inline constexpr char PeripheralShareKey[] { "PeripheralShare" };
inline constexpr char LinkDirectionKey[] { "LinkDirection" };
inline constexpr char TransferModeKey[] { "TransferMode" };
inline constexpr char StoragePathKey[] { "StoragePath" };
inline constexpr char ClipboardShareKey[] { "ClipboardShare" };
inline constexpr char CooperationEnabled[] { "CooperationEnabled" };

inline constexpr char CacheGroup[] { "Cache" };
inline constexpr char TransHistoryKey[] { "TransHistory" };
inline constexpr char ConnectHistoryKey[] { "ConnectHistory" };
inline constexpr char CloseOptionKey[] { "CloseOption" };

// 子网转发（subnet-forward）。v1 只读配置不做 UI，一切默认关闭；运行态一律
// 不落盘。设备白名单语义见 SubnetForwardConfig：列表为空即拒绝所有对端。
inline constexpr char SubnetForwardGroup[] { "SubnetForward" };
inline constexpr char SubnetForwardEnabled[] { "Enabled" };                     // 功能总开关
inline constexpr char SubnetForwardUsePeer[] { "UsePeerSubnet" };               // B 角色：允许本机流量经对端转发
inline constexpr char SubnetForwardServePeer[] { "ServePeer" };                 // A 角色：愿意为对端提供转发
inline constexpr char SubnetForwardNetworks[] { "Networks" };                   // CIDR 列表，空 = 功能不生效
inline constexpr char SubnetForwardAllowedHosts[] { "AllowedHosts" };           // 网段内再限 IP，空 = 整网段
inline constexpr char SubnetForwardAllowedPorts[] { "AllowedPorts" };           // 端口/范围列表，空 = 除拒绝外全部
inline constexpr char SubnetForwardDeniedPorts[] { "DeniedPorts" };             // 默认含 51598
inline constexpr char SubnetForwardDeviceWhitelist[] { "DeviceWhitelist" };     // 对端 IP 列表；空 = 拒绝所有对端
inline constexpr char SubnetForwardMaxStreams[] { "MaxStreams" };               // 并发流上限，默认 16
inline constexpr char SubnetForwardMaxRate[] { "MaxBytesPerSecond" };           // 全局带宽上限，0 = 不限
inline constexpr char SubnetForwardIdleTimeout[] { "IdleTimeoutMs" };           // 空闲流超时，0 = 不启用
inline constexpr char SubnetForwardDialTimeout[] { "DialTimeoutMs" };           // 拨号超时，0 = 默认 5s
inline constexpr char SubnetForwardAllowNonLocal[] { "AllowNonLocalNetworks" }; // 跳板防护：默认只放行本机所在网段
}

// 组策略
namespace DConfigKey {
inline constexpr char DiscoveryModeKey[] { "cooperation.discovery.mode" };
inline constexpr char TransferModeKey[] { "cooperation.transfer.mode" };
}

// 埋点属性
namespace ReportAttribute {
inline constexpr char CooperationStatus[] { "CooperationStatus" };
inline constexpr char FileDelivery[] { "FileDelivery" };
inline constexpr char ConnectionInfo[] { "ConnectionInfo" };
}

inline const char MainAppName[] { "dde-cooperation" };

// Setting menu action list
enum MenuAction {
    kSettings,
    kDownloadWindowClient,
    kDownloadMobileClient
};

enum CooperationMode {
    kPC = 0,
    kMobile
};

#endif   // TYPE_DEFINES_H
