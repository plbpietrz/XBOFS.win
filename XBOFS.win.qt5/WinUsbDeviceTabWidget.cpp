#include <qinputdialog.h>
#include <qmessagebox.h>

#include "WinUsbDeviceTabWidget.h"

WinUsbDeviceTabWidget::WinUsbDeviceTabWidget(
    QWidget *parent, QString devicePath, const XBOFSWin::WinUsbDevice *winUsbDevice, std::shared_ptr<spdlog::logger> logger
)
: QWidget(parent), winUsbDevice(winUsbDevice), logger(logger)
{
    configureBindingsDialog = new ConfigureBindingsDialog(this);
    configureGuideDownBindingsDialog = new ConfigureBindingsDialog(this);
    setObjectName(devicePath);
    ui.setupUi(this);
    connect(this, &WinUsbDeviceTabWidget::settingsChanged, winUsbDevice, &XBOFSWin::WinUsbDevice::refreshSettings);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::vigEmConnect, this, &WinUsbDeviceTabWidget::handleVigEmConnect);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::vigEmConnected, this, &WinUsbDeviceTabWidget::handleVigEmConnected);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::vigEmTargetAdd, this, &WinUsbDeviceTabWidget::handleVigEmTargetAdd);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::vigEmTargetAdded, this, &WinUsbDeviceTabWidget::handleVigEmTargetAdded);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::vigEmTargetInfo, this, &WinUsbDeviceTabWidget::handleVigEmTargetInfo);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::vigEmError, this, &WinUsbDeviceTabWidget::handleVigEmError);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceOpen, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceOpen);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceOpened, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceOpened);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceInfo, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceInfo);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceInit, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceInit);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceInitComplete, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceInitComplete);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceReadingInput, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceReadingInput);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceTerminating, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceTerminating);
    connect(winUsbDevice, &XBOFSWin::WinUsbDevice::winUsbDeviceError, this, &WinUsbDeviceTabWidget::handleWinUsbDeviceError);    
    connect(ui.bindingEnabledCheckBox, &QCheckBox::stateChanged, this, &WinUsbDeviceTabWidget::handleBindingEnabledCheckBoxStateChanged);
    connect(ui.activeProfileComboBox, &QComboBox::currentIndexChanged, this, &WinUsbDeviceTabWidget::handleActiveProfileComboBoxCurrentIndexChanged);
    connect(ui.addProfileButton, &QPushButton::clicked, this, &WinUsbDeviceTabWidget::handleAddProfilePushButtonClicked);
    connect(ui.deleteProfileButton, &QPushButton::clicked, this, &WinUsbDeviceTabWidget::handleDeleteProfilePushButtonClicked);
    connect(ui.configureBindingsButton, &QPushButton::clicked, this, &WinUsbDeviceTabWidget::handleConfigureBindingsPushButtonClicked);
    connect(ui.configureGuideDownBindingsButton, &QPushButton::clicked, this, &WinUsbDeviceTabWidget::handleConfigureGuideDownBindingsPushButtonClicked);
    connect(ui.debuggingEnabledCheckBox, &QCheckBox::stateChanged, this, &WinUsbDeviceTabWidget::handleDebuggingEnabledCheckBoxStateChanged);
}

WinUsbDeviceTabWidget::~WinUsbDeviceTabWidget()
{
    delete configureBindingsDialog;
    delete configureGuideDownBindingsDialog;
}

void WinUsbDeviceTabWidget::handleVigEmConnect(const std::wstring &devicePath) {
    ui.vigEmClientStatus->setText(QString::fromUtf8("Connecting..."));
}

void WinUsbDeviceTabWidget::handleVigEmConnected(const std::wstring &devicePath) {
    ui.vigEmClientStatus->setText(QString::fromUtf8("Connected"));
}

void WinUsbDeviceTabWidget::handleVigEmTargetAdd(const std::wstring &devicePath) {    
    ui.vigEmTargetStatus->setText(QString::fromUtf8("Adding..."));
}

void WinUsbDeviceTabWidget::handleVigEmTargetAdded(const std::wstring &devicePath) {
    ui.vigEmTargetStatus->setText(QString::fromUtf8("Added"));
}

void WinUsbDeviceTabWidget::handleVigEmTargetInfo(const std::wstring &devicePath, quint16 vendorId, quint16 productId, const ulong index) {
    ui.virtualDeviceVendorID->setText(QString::fromStdString(fmt::format("0x{:04X}", vendorId)));
    ui.virtualDeviceProductID->setText(QString::fromStdString(fmt::format("0x{:04X}", productId)));
    ui.virtualDeviceUserIndex->setText(QString::fromStdString(fmt::format("{}", index)));
}

void WinUsbDeviceTabWidget::handleVigEmError(const std::wstring &devicePath) {
    ui.vigEmTargetStatus->setText(QString::fromUtf8("Error!"));
}
    
void WinUsbDeviceTabWidget::handleWinUsbDeviceInfo(const std::wstring &devicePath, const QString &vendorId, const QString &vendorName,
                                                   const QString &productId, const QString &productName, const QString &serialNumber) {
    this->vendorId = vendorId;
    this->vendorName = vendorName;
    this->productId = productId;
    this->productName = productName;
    this->serialNumber = serialNumber;
    while (settings.group() != "") settings.endGroup();
    settings.beginGroup(QString("%1/%2/%3").arg(this->vendorId, this->productId, this->serialNumber));
    auto profiles = settings.childGroups();
    auto activeProfile = settings.value("activeProfile", "").toString();
    for (QStringList::const_iterator profileIterator = profiles.constBegin(); profileIterator != profiles.constEnd(); ++profileIterator) {
        auto profileName = *profileIterator;
        if (!settings.value(QString("%1/deleted").arg(profileName), true).toBool()) ui.activeProfileComboBox->addItem(profileName);
    }
    ui.activeProfileComboBox->setCurrentText(activeProfile);
    ui.vendorIdLabel->setText(this->vendorId);
    ui.productIdLabel->setText(this->productId);
    ui.manufacturerLabel->setText(this->vendorName);
    ui.productLabel->setText(this->productName);
    ui.serialNumberLabel->setText(this->serialNumber);    
    ui.bindingEnabledCheckBox->setEnabled(true);
    ui.bindingEnabledCheckBox->setChecked(settings.value("binding", false).toBool());
    ui.debuggingEnabledCheckBox->setEnabled(true);
    ui.debuggingEnabledCheckBox->setChecked(settings.value("debug", false).toBool());
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceOpen(const std::wstring &devicePath) {
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Opening..."));
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceOpened(const std::wstring &devicePath) {
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Open"));
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceInit(const std::wstring &devicePath) {
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Init"));
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceInitComplete(const std::wstring &devicePath) {
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Init complete"));
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceReadingInput(const std::wstring &devicePath) {
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Reading input..."));
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceTerminating(const std::wstring &devicePath) {
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Terminating..."));
}

void WinUsbDeviceTabWidget::handleWinUsbDeviceError(const std::wstring &devicePath) { 
    ui.winUsbDeviceStatus->setText(QString::fromUtf8("Error!"));
}

void WinUsbDeviceTabWidget::handleBindingEnabledCheckBoxStateChanged(int state) {
    settings.setValue("binding", (bool)state);
    ui.bindingProfileGroupBox->setEnabled((bool)state);
    ui.activeProfileComboBox->setEnabled((bool)state);
    emit settingsChanged();
}

void WinUsbDeviceTabWidget::handleDebuggingEnabledCheckBoxStateChanged(int state) {
    settings.setValue("debug", (bool)state);
    // TODO: Display/hide debugging info
    emit settingsChanged();
}

void WinUsbDeviceTabWidget::handleActiveProfileComboBoxCurrentIndexChanged(int index) {
    ui.configureBindingsButton->setEnabled(index > -1);
    ui.configureGuideDownBindingsButton->setEnabled(index > -1);
    settings.setValue("activeProfile", ui.activeProfileComboBox->currentText());
}

void WinUsbDeviceTabWidget::handleAddProfilePushButtonClicked(bool checked) {
    bool ok;
    QString profileName = QInputDialog::getText(this, "Add Profile", "Profile Name", QLineEdit::Normal, "", &ok);
    if (ok && !profileName.isEmpty() && ui.activeProfileComboBox->findText(profileName) == -1) {
        ui.activeProfileComboBox->addItem(profileName);
        settings.setValue(QString("%1/deleted").arg(profileName), false);
    }
}

void WinUsbDeviceTabWidget::handleDeleteProfilePushButtonClicked(bool checked) {
    if (!ui.activeProfileComboBox->count()) return;
    auto response = QMessageBox::question(this, XBOFSWin::SETTINGS_APPLICATION, "Are you sure?");
    if (response == QMessageBox::Yes) {
        settings.setValue(QString("%1/deleted").arg(ui.activeProfileComboBox->currentText()), true);
        ui.activeProfileComboBox->removeItem(ui.activeProfileComboBox->currentIndex());
    }

}

void WinUsbDeviceTabWidget::handleConfigureBindingsPushButtonClicked(bool checked) {
    configureBindingsDialog->open(vendorId, productId, productName, serialNumber, false);
}

void WinUsbDeviceTabWidget::handleConfigureGuideDownBindingsPushButtonClicked(bool checked) {
    configureGuideDownBindingsDialog->open(vendorId, productId, productName, serialNumber, true);
}
