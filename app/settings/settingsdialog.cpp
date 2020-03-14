/*
 * Copyright 2017  Smith AR <audoban@openmailbox.org>
 *                 Michail Vourlakos <mvourlakos@gmail.com>
 *
 * This file is part of Latte-Dock
 *
 * Latte-Dock is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * Latte-Dock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "settingsdialog.h"

// local
#include "universalsettings.h"
#include "ui_settingsdialog.h"
#include "../lattecorona.h"
#include "../screenpool.h"
#include "../layout/genericlayout.h"
#include "../layout/centrallayout.h"
#include "../layout/sharedlayout.h"
#include "../layouts/importer.h"
#include "../layouts/manager.h"
#include "../layouts/synchronizer.h"
#include "../liblatte2/types.h"
#include "../plasma/extended/theme.h"
#include "data/layoutdata.h"
#include "delegates/activitiesdelegate.h"
#include "delegates/backgroundcmbdelegate.h"
#include "delegates/checkboxdelegate.h"
#include "delegates/layoutnamedelegate.h"
#include "delegates/shareddelegate.h"
#include "tools/settingstools.h"

// Qt
#include <QButtonGroup>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTemporaryDir>

// KDE
#include <KActivities/Controller>
#include <KArchive/KTar>
#include <KArchive/KArchiveEntry>
#include <KArchive/KArchiveDirectory>
#include <KLocalizedString>
#include <KNotification>
#include <KWindowSystem>
#include <KNewStuff3/KNS3/DownloadDialog>

namespace Latte {

const int IDCOLUMN = 0;
const int HIDDENTEXTCOLUMN = 1;
const int COLORCOLUMN = 2;
const int NAMECOLUMN = 3;
const int MENUCOLUMN = 4;
const int BORDERSCOLUMN = 5;
const int ACTIVITYCOLUMN = 6;
const int SHAREDCOLUMN = 7;

const int SCREENTRACKERDEFAULTVALUE = 2500;
const int OUTLINEDEFAULTWIDTH = 1;

const QChar CheckMark{0x2714};

SettingsDialog::SettingsDialog(QWidget *parent, Latte::Corona *corona)
    : QDialog(parent),
      ui(new Ui::SettingsDialog),
      m_corona(corona)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    resize(m_corona->universalSettings()->layoutsWindowSize());

    connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked
            , this, &SettingsDialog::apply);
    connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked
            , this, &SettingsDialog::cancel);
    connect(ui->buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked
            , this, &SettingsDialog::ok);
    connect(ui->buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked
            , this, &SettingsDialog::restoreDefaults);

    m_layoutsController = new Settings::Controller::Layouts(this, m_corona);
    m_model = m_layoutsController->model();

    ui->layoutsView->setModel(m_layoutsController->model());
    ui->layoutsView->horizontalHeader()->setStretchLastSection(true);
    ui->layoutsView->verticalHeader()->setVisible(false);

    //connect(m_corona->layoutsManager(), &Layouts::Manager::currentLayoutNameChanged, this, &SettingsDialog::layoutsChanged);
    //connect(m_corona->layoutsManager(), &Layouts::Manager::centralLayoutsChanged, this, &SettingsDialog::layoutsChanged);

    QString iconsPath(m_corona->kPackage().path() + "../../plasmoids/org.kde.latte.containment/contents/icons/");

    //!find the available colors
    QDir layoutDir(iconsPath);
    QStringList filter;
    filter.append(QString("*print.jpg"));
    QStringList files = layoutDir.entryList(filter, QDir::Files | QDir::NoSymLinks);
    QStringList colors;

    for (auto &file : files) {
        int colorEnd = file.lastIndexOf("print.jpg");
        QString color = file.remove(colorEnd, 9);
        colors.append(color);
    }

    ui->layoutsView->setItemDelegateForColumn(NAMECOLUMN, new Settings::Layout::Delegate::LayoutName(this));
    ui->layoutsView->setItemDelegateForColumn(COLORCOLUMN, new Settings::Layout::Delegate::BackgroundCmbBox(this, iconsPath, colors));
    ui->layoutsView->setItemDelegateForColumn(MENUCOLUMN, new Settings::Layout::Delegate::CheckBox(this));
    ui->layoutsView->setItemDelegateForColumn(BORDERSCOLUMN, new Settings::Layout::Delegate::CheckBox(this));
    ui->layoutsView->setItemDelegateForColumn(ACTIVITYCOLUMN, new Settings::Layout::Delegate::Activities(this));
    ui->layoutsView->setItemDelegateForColumn(SHAREDCOLUMN, new Settings::Layout::Delegate::Shared(this));

    m_inMemoryButtons = new QButtonGroup(this);
    m_inMemoryButtons->addButton(ui->singleToolBtn, Latte::Types::SingleLayout);
    m_inMemoryButtons->addButton(ui->multipleToolBtn, Latte::Types::MultipleLayouts);
    m_inMemoryButtons->setExclusive(true);

    if (KWindowSystem::isPlatformWayland()) {
        m_inMemoryButtons->button(Latte::Types::MultipleLayouts)->setEnabled(false);
    }

    m_mouseSensitivityButtons = new QButtonGroup(this);
    m_mouseSensitivityButtons->addButton(ui->lowSensitivityBtn, Latte::Types::LowSensitivity);
    m_mouseSensitivityButtons->addButton(ui->mediumSensitivityBtn, Latte::Types::MediumSensitivity);
    m_mouseSensitivityButtons->addButton(ui->highSensitivityBtn, Latte::Types::HighSensitivity);
    m_mouseSensitivityButtons->setExclusive(true);

    ui->screenTrackerSpinBox->setValue(m_corona->universalSettings()->screenTrackerInterval());
    ui->outlineSpinBox->setValue(m_corona->themeExtended()->outlineWidth());

    //! About Menu
    QMenuBar *menuBar = new QMenuBar(this);
    // QMenuBar *rightAlignedMenuBar = new QMenuBar(menuBar);

    layout()->setMenuBar(menuBar);
    //menuBar->setCornerWidget(rightAlignedMenuBar);

    QMenu *fileMenu = new QMenu(i18n("File"), menuBar);
    menuBar->addMenu(fileMenu);

    QMenu *layoutMenu = new QMenu(i18n("Layout"), menuBar);
    menuBar->addMenu(layoutMenu);

    //! Help menu
    m_helpMenu = new KHelpMenu(menuBar);
    menuBar->addMenu(m_helpMenu->menu());
    //rightAlignedMenuBar->addMenu(helpMenu);

    //! hide help menu actions that are not used
    m_helpMenu->action(KHelpMenu::menuHelpContents)->setVisible(false);
    m_helpMenu->action(KHelpMenu::menuWhatsThis)->setVisible(false);


    QAction *screensAction = fileMenu->addAction(i18n("Sc&reens..."));
    screensAction->setIcon(QIcon::fromTheme("document-properties"));
    screensAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_R));

    QAction *quitAction = fileMenu->addAction(i18n("&Quit Latte"));
    quitAction->setIcon(QIcon::fromTheme("application-exit"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));

    m_editLayoutAction = layoutMenu->addAction(i18nc("edit layout","&Edit..."));
    m_editLayoutAction->setIcon(QIcon::fromTheme("document-edit"));
    m_editLayoutAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_E));
    m_editLayoutAction->setToolTip("You can edit layout file when layout is not active or locked");

    QAction *infoLayoutAction = layoutMenu->addAction(i18nc("layout information","&Information..."));
    infoLayoutAction->setIcon(QIcon::fromTheme("document-properties"));
    infoLayoutAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_I));

    //! RTL support for labels in preferences
    if (qApp->layoutDirection() == Qt::RightToLeft) {
        ui->behaviorLbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
        ui->mouseSensetivityLbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
        ui->delayLbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
    }

    loadSettings();

    //! SIGNALS

    connect(ui->layoutsView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [&]() {
        updatePerLayoutButtonsState();
        updateApplyButtonsState();
    });

    connect(m_inMemoryButtons, static_cast<void(QButtonGroup::*)(int, bool)>(&QButtonGroup::buttonToggled),
            [ = ](int id, bool checked) {

        m_model->setInMultipleMode(inMultipleLayoutsLook());

        updateApplyButtonsState();
        updateSharedLayoutsUiElements();
    });

    connect(m_mouseSensitivityButtons, static_cast<void(QButtonGroup::*)(int, bool)>(&QButtonGroup::buttonToggled),
            [ = ](int id, bool checked) {
        updateApplyButtonsState();
    });

    connect(ui->screenTrackerSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), [ = ](int i) {
        updateApplyButtonsState();
    });

    connect(ui->outlineSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), [ = ](int i) {
        updateApplyButtonsState();
    });

    connect(ui->autostartChkBox, &QCheckBox::stateChanged, this, &SettingsDialog::updateApplyButtonsState);
    connect(ui->badges3DStyleChkBox, &QCheckBox::stateChanged, this, &SettingsDialog::updateApplyButtonsState);
    connect(ui->metaPressChkBox, &QCheckBox::stateChanged, this, &SettingsDialog::updateApplyButtonsState);
    connect(ui->metaPressHoldChkBox, &QCheckBox::stateChanged, this, &SettingsDialog::updateApplyButtonsState);
    connect(ui->infoWindowChkBox, &QCheckBox::stateChanged, this, &SettingsDialog::updateApplyButtonsState);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &SettingsDialog::updateApplyButtonsState);

    connect(ui->noBordersForMaximizedChkBox, &QCheckBox::stateChanged, this, [&]() {
        bool noBordersForMaximized = ui->noBordersForMaximizedChkBox->isChecked();

        if (noBordersForMaximized) {
            ui->layoutsView->setColumnHidden(BORDERSCOLUMN, false);
        } else {
            ui->layoutsView->setColumnHidden(BORDERSCOLUMN, true);
        }

        updateApplyButtonsState();
    });

    connect(quitAction, &QAction::triggered, this, [&]() {
        close();
        m_corona->quitApplication();
    });

    connect(m_editLayoutAction, &QAction::triggered, this, [&]() {
        QString file = idForRow(ui->layoutsView->currentIndex().row());

        if (!file.isEmpty()) {
            QProcess::startDetached("kwrite \"" + file + "\"");
        }
    });

    connect(infoLayoutAction, &QAction::triggered, this, &SettingsDialog::showLayoutInformation);
    connect(screensAction, &QAction::triggered, this, &SettingsDialog::showScreensInformation);

    //! update all layouts view when runningActivities changed. This way we update immediately
    //! the running Activities in Activities checkboxes which are shown as bold
    connect(m_corona->activitiesConsumer(), &KActivities::Consumer::runningActivitiesChanged,
            this, [&]() {
        ui->layoutsView->update();
    });

    blockDeleteOnActivityStopped();
}

SettingsDialog::~SettingsDialog()
{
    qDebug() << Q_FUNC_INFO;

    qDeleteAll(m_layouts);

    if (m_model) {
        delete m_model;
    }

    if (m_corona && m_corona->universalSettings()) {
        m_corona->universalSettings()->setLayoutsWindowSize(size());

        QStringList columnWidths;       
        columnWidths << QString::number(ui->layoutsView->columnWidth(COLORCOLUMN));
        columnWidths << QString::number(ui->layoutsView->columnWidth(NAMECOLUMN));
        columnWidths << QString::number(ui->layoutsView->columnWidth(MENUCOLUMN));
        columnWidths << QString::number(ui->layoutsView->columnWidth(BORDERSCOLUMN));

        Latte::Types::LayoutsMemoryUsage inMemoryOption = static_cast<Latte::Types::LayoutsMemoryUsage>(m_inMemoryButtons->checkedId());

        if (inMemoryOption == Latte::Types::MultipleLayouts) {
            columnWidths << QString::number(ui->layoutsView->columnWidth(ACTIVITYCOLUMN));
        } else {
            //! In Single Mode, keed recorded value for ACTIVITYCOLUMN
            QStringList currentWidths = m_corona->universalSettings()->layoutsColumnWidths();
            if (currentWidths.count()>=5) {
                columnWidths << currentWidths[4];
            }
        }

        m_corona->universalSettings()->setLayoutsColumnWidths(columnWidths);
    }

    m_inMemoryButtons->deleteLater();
    m_mouseSensitivityButtons->deleteLater();

    for (const auto &tempDir : m_tempDirectories) {
        QDir tDir(tempDir);

        if (tDir.exists() && tempDir.startsWith("/tmp/")) {
            tDir.removeRecursively();
        }
    }
}

void SettingsDialog::blockDeleteOnActivityStopped()
{
    connect(m_corona->activitiesConsumer(), &KActivities::Consumer::runningActivitiesChanged,
            this, [&]() {
        m_blockDeleteOnReject = true;
        m_activityClosedTimer.start();
    });

    m_activityClosedTimer.setSingleShot(true);
    m_activityClosedTimer.setInterval(500);
    connect(&m_activityClosedTimer, &QTimer::timeout, this, [&]() {
        m_blockDeleteOnReject = false;
    });
}

QStringList SettingsDialog::availableSharesFor(int row)
{
    QStringList availables;
    QStringList regs;

    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString id = m_model->data(m_model->index(i, IDCOLUMN), Qt::DisplayRole).toString();
        QStringList shares = m_model->data(m_model->index(i, SHAREDCOLUMN), Qt::UserRole).toStringList();

        if (i != row) {
            if (shares.isEmpty()) {
                availables << id;
            } else {
                regs << shares;
            }
        }
    }

    for (const auto r : regs) {
        availables.removeAll(r);
    }

    return availables;
}

void SettingsDialog::toggleCurrentPage()
{
    if (ui->tabWidget->currentIndex() == 0) {
        ui->tabWidget->setCurrentIndex(1);
    } else {
        ui->tabWidget->setCurrentIndex(0);
    }                                   
}

void SettingsDialog::setCurrentPage(int page)
{
    ui->tabWidget->setCurrentIndex(page);
}

void SettingsDialog::on_newButton_clicked()
{
    qDebug() << Q_FUNC_INFO;

    //! find Default preset path
    for (const auto &preset : m_corona->layoutsManager()->presetsPaths()) {
        QString presetName = CentralLayout::layoutName(preset);

        if (presetName == "Default") {
            QByteArray presetNameChars = presetName.toUtf8();
            const char *prset_str = presetNameChars.data();
            presetName = uniqueLayoutName(i18n(prset_str));

            addLayoutForFile(preset, presetName, true, false);
            break;
        }
    }
}

void SettingsDialog::on_copyButton_clicked()
{
    qDebug() << Q_FUNC_INFO;

    int row = ui->layoutsView->currentIndex().row();

    if (row < 0) {
        return;
    }

    //! Update original layout before copying if this layout is active
    if (m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts) {
        QString lName = (m_model->data(m_model->index(row, NAMECOLUMN), Qt::DisplayRole)).toString();

        Layout::GenericLayout *generic = m_corona->layoutsManager()->synchronizer()->layout(lName);
        if (generic) {
            generic->syncToLayoutFile();
        }
    }

    Settings::Data::Layout original = m_model->at(row);
    Settings::Data::Layout copied = original;

    copied.setOriginalName(uniqueLayoutName(m_model->data(m_model->index(row, NAMECOLUMN), Qt::DisplayRole).toString()));
    copied.id = uniqueTempDirectory() + "/" + copied.originalName() + ".layout.latte";;
    copied.activities = QStringList();
    copied.isLocked = false;

    QFile(original.id).copy(copied.id);
    QFileInfo newFileInfo(copied.id);

    if (newFileInfo.exists() && !newFileInfo.isWritable()) {
        QFile(copied.id).setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }

    CentralLayout *settings = new CentralLayout(this, copied.id);
    m_layouts[copied.id] = settings;

    m_model->appendLayout(copied);

    ui->layoutsView->selectRow(row + 1);
}

void SettingsDialog::on_downloadButton_clicked()
{
    qDebug() << Q_FUNC_INFO;

    KNS3::DownloadDialog dialog(QStringLiteral("latte-layouts.knsrc"), this);
    dialog.resize(m_corona->universalSettings()->downloadWindowSize());
    dialog.exec();

    bool layoutAdded{false};

    if (!dialog.changedEntries().isEmpty() || !dialog.installedEntries().isEmpty()) {
        for (const auto &entry : dialog.installedEntries()) {
            for (const auto &entryFile : entry.installedFiles()) {
                Layouts::Importer::LatteFileVersion version = Layouts::Importer::fileVersion(entryFile);

                if (version == Layouts::Importer::LayoutVersion2) {
                    layoutAdded = true;
                    addLayoutForFile(entryFile);
                    break;
                }
            }
        }
    }

    m_corona->universalSettings()->setDownloadWindowSize(dialog.size());

    if (layoutAdded) {
        apply();
    }
}

void SettingsDialog::on_removeButton_clicked()
{
    qDebug() << Q_FUNC_INFO;

    int row = ui->layoutsView->currentIndex().row();

    if (row < 0) {
        return;
    }

    QString layoutName = m_model->data(m_model->index(row, NAMECOLUMN), Qt::DisplayRole).toString();

    if (m_corona->layoutsManager()->synchronizer()->centralLayout(layoutName)) {
        return;
    }

    m_model->removeRow(row);

    updateApplyButtonsState();

    row = qMax(row - 1, 0);

    ui->layoutsView->selectRow(row);
}

void SettingsDialog::on_lockedButton_clicked()
{
    qDebug() << Q_FUNC_INFO;

    int row = ui->layoutsView->currentIndex().row();

    if (row < 0) {
        return;
    }

    bool lockedModel = m_model->data(m_model->index(row, NAMECOLUMN), Settings::Model::Layouts::LAYOUTISLOCKEDROLE).toBool();

    m_model->setData(m_model->index(row, NAMECOLUMN), !lockedModel, Settings::Model::Layouts::LAYOUTISLOCKEDROLE);

    updatePerLayoutButtonsState();
    updateApplyButtonsState();
}

void SettingsDialog::on_sharedButton_clicked()
{
    qDebug() << Q_FUNC_INFO;

    int row = ui->layoutsView->currentIndex().row();

    if (row < 0) {
        return;
    }

    if (isShared(row)) {
        m_model->setData(m_model->index(row, SHAREDCOLUMN), QStringList(), Qt::UserRole);
    } else {
        bool assigned{false};
        QStringList assignedList;

        QStringList availableShares = availableSharesFor(row);

        for (const auto &id : availableShares) {
            QString name = nameForId(id);
            if (m_corona->layoutsManager()->synchronizer()->layout(name)) {
                assignedList << id;
                m_model->setData(m_model->index(row, SHAREDCOLUMN), assignedList, Qt::UserRole);
                assigned = true;
                break;
            }
        }

        if (!assigned && availableShares.count()>0) {
            assignedList << availableShares[0];
            m_model->setData(m_model->index(row, SHAREDCOLUMN), assignedList, Qt::UserRole);
            assigned = true;
        }
    }

    updatePerLayoutButtonsState();
    updateApplyButtonsState();
}

void SettingsDialog::on_importButton_clicked()
{
    qDebug() << Q_FUNC_INFO;


    QFileDialog *fileDialog = new QFileDialog(this, i18nc("import layout/configuration", "Import Layout/Configuration")
                                              , QDir::homePath()
                                              , QStringLiteral("layout.latte"));

    fileDialog->setFileMode(QFileDialog::AnyFile);
    fileDialog->setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog->setDefaultSuffix("layout.latte");

    QStringList filters;
    filters << QString(i18nc("import latte layout", "Latte Dock Layout file v0.2") + "(*.layout.latte)")
            << QString(i18nc("import latte layouts/configuration", "Latte Dock Full Configuration file (v0.1, v0.2)") + "(*.latterc)");
    fileDialog->setNameFilters(filters);

    connect(fileDialog, &QFileDialog::finished
            , fileDialog, &QFileDialog::deleteLater);

    connect(fileDialog, &QFileDialog::fileSelected
            , this, [&](const QString & file) {
        Layouts::Importer::LatteFileVersion version = Layouts::Importer::fileVersion(file);
        qDebug() << "VERSION :::: " << version;

        if (version == Layouts::Importer::LayoutVersion2) {
            addLayoutForFile(file);
        } else if (version == Layouts::Importer::ConfigVersion1) {
            auto msg = new QMessageBox(this);
            msg->setIcon(QMessageBox::Warning);
            msg->setWindowTitle(i18n("Import: Configuration file version v0.1"));
            msg->setText(
                        i18n("You are going to import an old version <b>v0.1</b> configuration file.<br><b>Be careful</b>, importing the entire configuration <b>will erase all</b> your current configuration!!!<br><br> <i>Alternative, you can <b>import safely</b> from this file<br><b>only the contained layouts...</b></i>"));
            msg->setStandardButtons(QMessageBox::Cancel);

            QPushButton *fullBtn = new QPushButton(msg);
            QPushButton *layoutsBtn = new QPushButton(msg);
            fullBtn->setText(i18nc("import full configuration", "Full Configuration"));
            fullBtn->setIcon(QIcon::fromTheme("settings"));
            layoutsBtn->setText(i18nc("import only the layouts", "Only Layouts"));
            layoutsBtn->setIcon(QIcon::fromTheme("user-identity"));

            msg->addButton(fullBtn, QMessageBox::AcceptRole);
            msg->addButton(layoutsBtn, QMessageBox::AcceptRole);

            msg->setDefaultButton(layoutsBtn);

            connect(msg, &QMessageBox::finished, msg, &QMessageBox::deleteLater);

            msg->open();

            connect(layoutsBtn, &QPushButton::clicked
                    , this, [ &, file](bool check) {
                importLayoutsFromV1ConfigFile(file);
            });

            connect(fullBtn, &QPushButton::clicked
                    , this, [ &, file](bool check) {
                //!NOTE: Restart latte for import the new configuration
                QProcess::startDetached(qGuiApp->applicationFilePath() + " --import-full \"" + file + "\"");
                qGuiApp->exit();
            });
        } else if (version == Layouts::Importer::ConfigVersion2) {
            auto msg = new QMessageBox(this);
            msg->setIcon(QMessageBox::Warning);
            msg->setWindowTitle(i18n("Import: Configuration file version v0.2"));
            msg->setText(
                        i18n("You are going to import a <b>v0.2</b> configuration file.<br><b>Be careful</b>, importing <b>will erase all</b> your current configuration!!!<br><br><i>Would you like to proceed?</i>"));
            msg->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msg->setDefaultButton(QMessageBox::No);

            connect(msg, &QMessageBox::finished, this, [ &, msg, file](int result) {
                if (result == QMessageBox::Yes) {
                    //!NOTE: Restart latte for import the new configuration
                    msg->deleteLater();
                    QProcess::startDetached(qGuiApp->applicationFilePath() + " --import-full \"" + file + "\"");
                    qGuiApp->exit();
                }
            });

            msg->open();
        }
    });

    fileDialog->open();
}

bool SettingsDialog::importLayoutsFromV1ConfigFile(QString file)
{
    KTar archive(file, QStringLiteral("application/x-tar"));
    archive.open(QIODevice::ReadOnly);

    //! if the file isnt a tar archive
    if (archive.isOpen()) {
        QDir tempDir{uniqueTempDirectory()};

        const auto archiveRootDir = archive.directory();

        for (const auto &name : archiveRootDir->entries()) {
            auto fileEntry = archiveRootDir->file(name);
            fileEntry->copyTo(tempDir.absolutePath());
        }

        QString name = Layouts::Importer::nameOfConfigFile(file);

        QString applets(tempDir.absolutePath() + "/" + "lattedock-appletsrc");

        if (QFile(applets).exists()) {
            if (m_corona->layoutsManager()->importer()->importOldLayout(applets, name, false, tempDir.absolutePath())) {
                addLayoutForFile(tempDir.absolutePath() + "/" + name + ".layout.latte", name, false);
            }

            QString alternativeName = name + "-" + i18nc("layout", "Alternative");

            if (m_corona->layoutsManager()->importer()->importOldLayout(applets, alternativeName, false, tempDir.absolutePath())) {
                addLayoutForFile(tempDir.absolutePath() + "/" + alternativeName + ".layout.latte", alternativeName, false);
            }
        }

        return true;
    }

    return false;
}


void SettingsDialog::on_exportButton_clicked()
{
    int row = ui->layoutsView->currentIndex().row();

    if (row < 0) {
        return;
    }

    QString layoutExported = m_model->data(m_model->index(row, IDCOLUMN), Qt::DisplayRole).toString();

    //! Update ALL active original layouts before exporting,
    //! this is needed because the export method can export also the full configuration
    qDebug() << Q_FUNC_INFO;

    m_corona->layoutsManager()->synchronizer()->syncActiveLayoutsToOriginalFiles();

    QFileDialog *fileDialog = new QFileDialog(this, i18nc("export layout/configuration", "Export Layout/Configuration")
                                              , QDir::homePath(), QStringLiteral("layout.latte"));

    fileDialog->setFileMode(QFileDialog::AnyFile);
    fileDialog->setAcceptMode(QFileDialog::AcceptSave);
    fileDialog->setDefaultSuffix("layout.latte");

    QStringList filters;
    QString filter1(i18nc("export layout", "Latte Dock Layout file v0.2") + "(*.layout.latte)");
    QString filter2(i18nc("export full configuration", "Latte Dock Full Configuration file v0.2") + "(*.latterc)");

    filters << filter1
            << filter2;

    fileDialog->setNameFilters(filters);

    connect(fileDialog, &QFileDialog::finished
            , fileDialog, &QFileDialog::deleteLater);

    connect(fileDialog, &QFileDialog::fileSelected
            , this, [ &, layoutExported](const QString & file) {
        auto showNotificationError = []() {
            auto notification = new KNotification("export-fail", KNotification::CloseOnTimeout);
            notification->setText(i18nc("export layout", "Failed to export layout"));
            notification->sendEvent();
        };

        if (QFile::exists(file) && !QFile::remove(file)) {
            showNotificationError();
            return;
        }

        if (file.endsWith(".layout.latte")) {
            if (!QFile(layoutExported).copy(file)) {
                showNotificationError();
                return;
            }

            QFileInfo newFileInfo(file);

            if (newFileInfo.exists() && !newFileInfo.isWritable()) {
                QFile(file).setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
            }

            CentralLayout layoutS(this, file);
            layoutS.setActivities(QStringList());
            layoutS.clearLastUsedActivity();

            //NOTE: The pointer is automatically deleted when the event is closed
            auto notification = new KNotification("export-done", KNotification::CloseOnTimeout);
            notification->setActions({i18nc("export layout", "Open location")});
            notification->setText(i18nc("export layout", "Layout exported successfully"));

            connect(notification, &KNotification::action1Activated
                    , this, [file]() {
                QDesktopServices::openUrl({QFileInfo(file).canonicalPath()});
            });

            notification->sendEvent();
        } else if (file.endsWith(".latterc")) {
            auto showNotificationError = []() {
                auto notification = new KNotification("export-fail", KNotification::CloseOnTimeout);
                notification->setText(i18nc("import/export config", "Failed to export configuration"));
                notification->sendEvent();
            };

            if (m_corona->layoutsManager()->importer()->exportFullConfiguration(file)) {

                auto notification = new KNotification("export-done", KNotification::CloseOnTimeout);
                notification->setActions({i18nc("import/export config", "Open location")});
                notification->setText(i18nc("import/export config", "Full Configuration exported successfully"));

                connect(notification, &KNotification::action1Activated
                        , this, [file]() {
                    QDesktopServices::openUrl({QFileInfo(file).canonicalPath()});
                });

                notification->sendEvent();
            } else {
                showNotificationError();
            }
        }
    });


    fileDialog->open();
}

void SettingsDialog::requestImagesDialog(int row)
{
    QStringList mimeTypeFilters;
    mimeTypeFilters << "image/jpeg" // will show "JPEG image (*.jpeg *.jpg)
                    << "image/png";  // will show "PNG image (*.png)"

    QFileDialog dialog(this);
    dialog.setMimeTypeFilters(mimeTypeFilters);

    QString background = m_model->data(m_model->index(row, COLORCOLUMN), Qt::BackgroundRole).toString();

    if (background.startsWith("/") && QFileInfo(background).exists()) {
        dialog.setDirectory(QFileInfo(background).absolutePath());
        dialog.selectFile(background);
    }

    if (dialog.exec()) {
        QStringList files = dialog.selectedFiles();

        if (files.count() > 0) {
            m_model->setData(m_model->index(row, COLORCOLUMN), files[0], Qt::BackgroundRole);
        }
    }
}

void SettingsDialog::requestColorsDialog(int row)
{
    QColorDialog dialog(this);
    QString textColor = m_model->data(m_model->index(row, COLORCOLUMN), Qt::UserRole).toString();
    dialog.setCurrentColor(QColor(textColor));

    if (dialog.exec()) {
        qDebug() << dialog.selectedColor().name();
        m_model->setData(m_model->index(row, COLORCOLUMN), dialog.selectedColor().name(), Qt::UserRole);
    }
}

void SettingsDialog::accept()
{
    //! disable accept totally in order to avoid closing with ENTER key with no real reason
    qDebug() << Q_FUNC_INFO;
}


void SettingsDialog::cancel()
{
    qDebug() << Q_FUNC_INFO;

    if (!m_blockDeleteOnReject) {
        deleteLater();
    }
}

void SettingsDialog::ok()
{
    if (!ui->buttonBox->button(QDialogButtonBox::Ok)->hasFocus()) {
        return;
    }

    qDebug() << Q_FUNC_INFO;

    if (saveAllChanges()) {
        deleteLater();
    }
}

void SettingsDialog::apply()
{
    qDebug() << Q_FUNC_INFO;
    saveAllChanges();

    o_settingsOriginalData = currentSettings();
    o_layoutsOriginalData = m_model->currentData();

    updateApplyButtonsState();
    updatePerLayoutButtonsState();
}

void SettingsDialog::restoreDefaults()
{
    qDebug() << Q_FUNC_INFO;

    if (ui->tabWidget->currentIndex() == 0) {
        //! Default layouts missing from layouts list
        for (const auto &preset : m_corona->layoutsManager()->presetsPaths()) {
            QString presetName = CentralLayout::layoutName(preset);
            QByteArray presetNameChars = presetName.toUtf8();
            const char *prset_str = presetNameChars.data();
            presetName = i18n(prset_str);

            if (!nameExistsInModel(presetName)) {
                addLayoutForFile(preset, presetName);
            }
        }
    } else if (ui->tabWidget->currentIndex() == 1) {
        //! Defaults for general Latte settings
        ui->autostartChkBox->setChecked(true);
        ui->badges3DStyleChkBox->setChecked(true);
        ui->infoWindowChkBox->setChecked(true);
        ui->metaPressChkBox->setChecked(false);
        ui->metaPressHoldChkBox->setChecked(true);
        ui->noBordersForMaximizedChkBox->setChecked(false);
        ui->highSensitivityBtn->setChecked(true);
        ui->screenTrackerSpinBox->setValue(SCREENTRACKERDEFAULTVALUE);
        ui->outlineSpinBox->setValue(OUTLINEDEFAULTWIDTH);
    }
}

void SettingsDialog::addLayoutForFile(QString file, QString layoutName, bool newTempDirectory, bool showNotification)
{
    if (layoutName.isEmpty()) {
        layoutName = CentralLayout::layoutName(file);
    }

    Settings::Data::Layout copied;

    if (newTempDirectory) {
        copied.id = uniqueTempDirectory() + "/" + layoutName + ".layout.latte";
        QFile(file).copy(copied.id);
    } else {
        copied.id = file;
    }

    QFileInfo newFileInfo(copied.id);

    if (newFileInfo.exists() && !newFileInfo.isWritable()) {
        QFile(copied.id).setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }

    if (m_layouts.contains(copied.id)) {
        CentralLayout *oldSettings = m_layouts.take(copied.id);
        delete oldSettings;
    }

    CentralLayout *settings = new CentralLayout(this, copied.id);
    m_layouts[copied.id] = settings;

    copied.setOriginalName(uniqueLayoutName(layoutName));
    copied.color = settings->color();
    copied.textColor = settings->textColor();
    copied.background = settings->background();
    copied.isLocked = !settings->isWritable();
    copied.isShownInMenu = settings->showInMenu();
    copied.hasDisabledBorders = settings->disableBordersForMaximizedWindows();

    if (copied.background.isEmpty()) {
        copied.textColor = QString();
    }

    m_model->appendLayout(copied);

  //  ui->layoutsView->selectRow(row);

    if (showNotification) {
        //NOTE: The pointer is automatically deleted when the event is closed
        auto notification = new KNotification("import-done", KNotification::CloseOnTimeout);
        notification->setText(i18nc("import-done", "Layout: <b>%0</b> imported successfully<br>").arg(layoutName));
        notification->sendEvent();
    }
}

void SettingsDialog::loadSettings()
{
    m_model->clear();

    //! The shares map needs to be constructed for start/scratch.
    //! We start feeding information with layout_names and during the process
    //! we update them to valid layout_ids
    Layouts::SharesMap sharesMap;

    int i = 0;
    QStringList brokenLayouts;

    if (m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts) {
        m_corona->layoutsManager()->synchronizer()->syncActiveLayoutsToOriginalFiles();
    }

    Settings::Data::LayoutsTable layoutsBuffer;

    for (const auto layout : m_corona->layoutsManager()->layouts()) {
        Settings::Data::Layout original;
        original.id = QDir::homePath() + "/.config/latte/" + layout + ".layout.latte";

        CentralLayout *central = new CentralLayout(this, original.id);

        original.setOriginalName(central->name());
        original.background = central->background();
        original.color = central->color();
        original.textColor = central->textColor();
        original.isActive = (m_corona->layoutsManager()->synchronizer()->layout(original.originalName()) != nullptr);
        original.isLocked = !central->isWritable();
        original.isShownInMenu = central->showInMenu();
        original.hasDisabledBorders = central->disableBordersForMaximizedWindows();
        original.activities = central->activities();

        //! add central layout properties
        if (original.background.isEmpty()) {
            original.textColor = QString();
        }

        m_layouts[original.id] = central;

        //! create initial SHARES maps
        QString shared = central->sharedLayoutName();
        if (!shared.isEmpty()) {
            sharesMap[shared].append(original.id);
        }

        layoutsBuffer << original;

        qDebug() << "counter:" << i << " total:" << m_model->rowCount();

        i++;

        Layout::GenericLayout *generic = m_corona->layoutsManager()->synchronizer()->layout(central->name());

        if ((generic && generic->layoutIsBroken()) || (!generic && central->layoutIsBroken())) {
            brokenLayouts.append(central->name());
        }
    }

    //! update SHARES map keys in order to use the #settingsid(s)
    QStringList tempSharedNames;

    //! remove these records after updating
    for (QHash<const QString, QStringList>::iterator i=sharesMap.begin(); i!=sharesMap.end(); ++i) {
        tempSharedNames << i.key();
    }

    //! update keys
    for (QHash<const QString, QStringList>::iterator i=sharesMap.begin(); i!=sharesMap.end(); ++i) {
        QString shareid = layoutsBuffer.idForOriginalName(i.key());
        if (!shareid.isEmpty()) {
            sharesMap[shareid] = i.value();
        }
    }

    //! remove deprecated keys
    for (const auto &key : tempSharedNames) {
        sharesMap.remove(key);
    }

    qDebug() << "SHARES MAP ::: " << sharesMap;

    for (QHash<const QString, QStringList>::iterator i=sharesMap.begin(); i!=sharesMap.end(); ++i) {
        layoutsBuffer[i.key()].shares = i.value();
    }

    //! Send original loaded data to model
    m_model->setCurrentData(layoutsBuffer);

    ui->layoutsView->selectRow(rowForName(m_corona->layoutsManager()->currentLayoutName()));

    //! this line should be commented for debugging layouts window functionality
    ui->layoutsView->setColumnHidden(IDCOLUMN, true);
    ui->layoutsView->setColumnHidden(HIDDENTEXTCOLUMN, true);

    if (m_corona->universalSettings()->canDisableBorders()) {
        ui->layoutsView->setColumnHidden(BORDERSCOLUMN, false);
    } else {
        ui->layoutsView->setColumnHidden(BORDERSCOLUMN, true);
    }

    ui->layoutsView->resizeColumnsToContents();

    QStringList columnWidths = m_corona->universalSettings()->layoutsColumnWidths();

    if (!columnWidths.isEmpty()) {
        for (int i=0; i<qMin(columnWidths.count(),4); ++i) {
            ui->layoutsView->setColumnWidth(COLORCOLUMN+i, columnWidths[i].toInt());
        }
    }

    bool inMultiple{m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts};

    if (inMultiple) {
        ui->multipleToolBtn->setChecked(true);
    } else {
        ui->singleToolBtn->setChecked(true);
    }

    m_model->setInMultipleMode(inMultiple);

    updatePerLayoutButtonsState();

    ui->autostartChkBox->setChecked(m_corona->universalSettings()->autostart());
    ui->badges3DStyleChkBox->setChecked(m_corona->universalSettings()->badges3DStyle());
    ui->infoWindowChkBox->setChecked(m_corona->universalSettings()->showInfoWindow());
    ui->metaPressChkBox->setChecked(m_corona->universalSettings()->kwin_metaForwardedToLatte());
    ui->metaPressHoldChkBox->setChecked(m_corona->universalSettings()->metaPressAndHoldEnabled());
    ui->noBordersForMaximizedChkBox->setChecked(m_corona->universalSettings()->canDisableBorders());

    if (m_corona->universalSettings()->mouseSensitivity() == Types::LowSensitivity) {
        ui->lowSensitivityBtn->setChecked(true);
    } else if (m_corona->universalSettings()->mouseSensitivity() == Types::MediumSensitivity) {
        ui->mediumSensitivityBtn->setChecked(true);
    } else if (m_corona->universalSettings()->mouseSensitivity() == Types::HighSensitivity) {
        ui->highSensitivityBtn->setChecked(true);
    }

    o_settingsOriginalData = currentSettings();
    o_layoutsOriginalData = m_model->currentData();
    updateApplyButtonsState();
    updateSharedLayoutsUiElements();

    //! there are broken layouts and the user must be informed!
    if (brokenLayouts.count() > 0) {
        auto msg = new QMessageBox(this);
        msg->setIcon(QMessageBox::Warning);
        msg->setWindowTitle(i18n("Layout Warning"));
        msg->setText(i18n("The layout(s) <b>%0</b> have <i>broken configuration</i>!!! Please <b>remove them</b> to improve the system stability...").arg(brokenLayouts.join(",")));
        msg->setStandardButtons(QMessageBox::Ok);

        msg->open();
    }
}

QList<int> SettingsDialog::currentSettings()
{
    QList<int> settings;
    settings << m_inMemoryButtons->checkedId();
    settings << (int)ui->autostartChkBox->isChecked();
    settings << (int)ui->badges3DStyleChkBox->isChecked();
    settings << (int)ui->infoWindowChkBox->isChecked();
    settings << (int)ui->metaPressChkBox->isChecked();
    settings << (int)ui->metaPressHoldChkBox->isChecked();
    settings << (int)ui->noBordersForMaximizedChkBox->isChecked();
    settings << m_mouseSensitivityButtons->checkedId();
    settings << ui->screenTrackerSpinBox->value();
    settings << ui->outlineSpinBox->value();
    settings << m_model->rowCount();

    return settings;
}

void SettingsDialog::appendLayout(Settings::Data::Layout &layout)
{
    m_model->appendLayout(layout);
}

void SettingsDialog::on_switchButton_clicked()
{
    int currentIndex = ui->layoutsView->currentIndex().row();
    QStringList currentActivities = m_model->data(m_model->index(ui->layoutsView->currentIndex().row(), ACTIVITYCOLUMN), Qt::UserRole).toStringList();

    if (ui->buttonBox->button(QDialogButtonBox::Apply)->isEnabled()) {
        //! thus there are changes in the settings

        QString lName;
        QStringList lActivities;

        if (m_inMemoryButtons->checkedId() == Latte::Types::MultipleLayouts) {
            lName = m_model->data(m_model->index(ui->layoutsView->currentIndex().row(), NAMECOLUMN), Qt::DisplayRole).toString();
            lActivities = m_model->data(m_model->index(ui->layoutsView->currentIndex().row(), ACTIVITYCOLUMN), Qt::UserRole).toStringList();
        }

        apply();

        if (!lName.isEmpty() && !lActivities.isEmpty()) {
            //! an activities-assigned layout is chosen and at the same time we are moving
            //! to multiple layouts state
            m_corona->layoutsManager()->switchToLayout(lName);
        }
    } else {
        QVariant value = m_model->data(m_model->index(ui->layoutsView->currentIndex().row(), NAMECOLUMN), Qt::DisplayRole);

        if (value.isValid()) {
            m_corona->layoutsManager()->switchToLayout(value.toString());
        } else {
            qDebug() << "not valid layout";
        }
    }

    updatePerLayoutButtonsState();
}

void SettingsDialog::on_pauseButton_clicked()
{
    ui->pauseButton->setEnabled(false);

    QString id = m_model->data(m_model->index(ui->layoutsView->currentIndex().row(), IDCOLUMN), Qt::DisplayRole).toString();
    CentralLayout *layout = m_layouts[id];

    if (layout) {
        m_corona->layoutsManager()->synchronizer()->pauseLayout(layout->name());
    }
}


void SettingsDialog::updateApplyButtonsState()
{
    bool changed{false};

    //! Ok, Apply Buttons
    if ((o_settingsOriginalData != currentSettings())
            || (o_layoutsOriginalData != m_model->currentData())) {
        changed = true;
    }

    if (changed) {
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
        ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    } else {
        //ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    }

    //! RestoreDefaults Button
    if (ui->tabWidget->currentIndex() == 0) {
        //! Check Default layouts missing from layouts list

        bool layoutMissing{false};

        for (const auto &preset : m_corona->layoutsManager()->presetsPaths()) {
            QString presetName = CentralLayout::layoutName(preset);
            QByteArray presetNameChars = presetName.toUtf8();
            const char *prset_str = presetNameChars.data();
            presetName = i18n(prset_str);

            if (!nameExistsInModel(presetName)) {
                layoutMissing = true;
                break;
            }
        }

        if (layoutMissing) {
            ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(true);
        } else {
            ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(false);
        }
    } else if (ui->tabWidget->currentIndex() == 1) {
        //! Defaults for general Latte settings

        if (!ui->autostartChkBox->isChecked()
                || ui->badges3DStyleChkBox->isChecked()
                || ui->metaPressChkBox->isChecked()
                || !ui->metaPressHoldChkBox->isChecked()
                || !ui->infoWindowChkBox->isChecked()
                || ui->noBordersForMaximizedChkBox->isChecked()
                || !ui->highSensitivityBtn->isChecked()
                || ui->screenTrackerSpinBox->value() != SCREENTRACKERDEFAULTVALUE
                || ui->outlineSpinBox->value() != OUTLINEDEFAULTWIDTH ) {
            ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(true);
        } else {
            ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(false);
        }
    }
}

void SettingsDialog::updatePerLayoutButtonsState()
{
    int currentRow = ui->layoutsView->currentIndex().row();

    QString id = m_model->data(m_model->index(currentRow, IDCOLUMN), Qt::DisplayRole).toString();
    QString nameInModel = m_model->data(m_model->index(currentRow, NAMECOLUMN), Qt::DisplayRole).toString();
    QString originalName = o_layoutsOriginalData.contains(id) ? o_layoutsOriginalData[id].originalName() : "";
    bool lockedInModel = m_model->data(m_model->index(currentRow, NAMECOLUMN), Qt::UserRole).toBool();
    bool sharedInModel = !m_model->data(m_model->index(currentRow, SHAREDCOLUMN), Qt::UserRole).toStringList().isEmpty();
    bool editable = !isActive(originalName) && !lockedInModel;

    Latte::Types::LayoutsMemoryUsage inMemoryOption = static_cast<Latte::Types::LayoutsMemoryUsage>(m_inMemoryButtons->checkedId());

    //! Switch Button
    if (id.startsWith("/tmp/")
            || originalName != nameInModel
            || (inMemoryOption == Types::MultipleLayouts && sharedInModel)
            || (m_corona->layoutsManager()->synchronizer()->currentLayoutName() == originalName)) {
        ui->switchButton->setEnabled(false);
    } else {
        ui->switchButton->setEnabled(true);
    }

    //! Pause Button
    if (m_corona->layoutsManager()->memoryUsage() == Types::SingleLayout) {
        ui->pauseButton->setVisible(false);
    } else if (m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts) {
        ui->pauseButton->setVisible(true);

        QStringList lActivities = m_model->data(m_model->index(currentRow, ACTIVITYCOLUMN), Qt::UserRole).toStringList();

        Latte::CentralLayout *layout = m_layouts[id];

        if (!lActivities.isEmpty() && layout && m_corona->layoutsManager()->synchronizer()->centralLayout(originalName)) {
            ui->pauseButton->setEnabled(true);
        } else {
            ui->pauseButton->setEnabled(false);
        }
    }

    //! Remove Layout Button
    if ((originalName == m_corona->layoutsManager()->currentLayoutName())
            || (m_corona->layoutsManager()->synchronizer()->centralLayout(originalName))
            || lockedInModel) {
        ui->removeButton->setEnabled(false);
    } else {
        ui->removeButton->setEnabled(true);
    }

    //! Layout Locked Button
    if (lockedInModel) {
        ui->lockedButton->setChecked(true);
    } else {
        ui->lockedButton->setChecked(false);
    }

    //! Layout Shared Button
    if (sharedInModel) {
        ui->sharedButton->setChecked(true);
    } else {
        ui->sharedButton->setChecked(false);
    }

    if (editable) {
        m_editLayoutAction->setEnabled(true);
    } else {
        m_editLayoutAction->setEnabled(false);
    }
}

void SettingsDialog::updateSharedLayoutsUiElements()
{
    //! UI Elements that need to be enabled/disabled

    Latte::Types::LayoutsMemoryUsage inMemoryOption = static_cast<Latte::Types::LayoutsMemoryUsage>(m_inMemoryButtons->checkedId());
    if (inMemoryOption == Latte::Types::MultipleLayouts) {
        ui->layoutsView->setColumnHidden(SHAREDCOLUMN, false);
        ui->sharedButton->setVisible(true);

        //! column widths
        QStringList cWidths = m_corona->universalSettings()->layoutsColumnWidths();

        if (cWidths.count()>=5) {
            ui->layoutsView->setColumnWidth(ACTIVITYCOLUMN, cWidths[4].toInt());
        }
    } else {
        ui->layoutsView->setColumnHidden(SHAREDCOLUMN, true);
        ui->sharedButton->setVisible(false);
    }
}

bool SettingsDialog::dataAreAccepted()
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString layout1 = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();

        for (int j = i + 1; j < m_model->rowCount(); ++j) {
            QString temp = m_model->data(m_model->index(j, NAMECOLUMN), Qt::DisplayRole).toString();

            //!same layout name exists again
            if (layout1 == temp) {
                auto msg = new QMessageBox(this);
                msg->setIcon(QMessageBox::Warning);
                msg->setWindowTitle(i18n("Layout Warning"));
                msg->setText(i18n("There are layouts with the same name, that is not permitted!!! Please update these names to re-apply the changes..."));
                msg->setStandardButtons(QMessageBox::Ok);

                connect(msg, &QMessageBox::finished, this, [ &, i, j](int result) {
                    QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect;
                    QModelIndex indexBase = m_model->index(i, NAMECOLUMN);
                    ui->layoutsView->selectionModel()->select(indexBase, flags);

                    QModelIndex indexOccurence = m_model->index(j, NAMECOLUMN);
                    ui->layoutsView->edit(indexOccurence);
                });


                msg->open();

                return false;
            }
        }
    }

    return true;
}

void SettingsDialog::showLayoutInformation()
{
    int currentRow = ui->layoutsView->currentIndex().row();

    QString id = m_model->data(m_model->index(currentRow, IDCOLUMN), Qt::DisplayRole).toString();
    QString name = m_model->data(m_model->index(currentRow, NAMECOLUMN), Qt::DisplayRole).toString();

    Layout::GenericLayout *genericActive= m_corona->layoutsManager()->synchronizer()->layout(o_layoutsOriginalData[id].originalName());
    Layout::GenericLayout *generic = genericActive ? genericActive : m_layouts[id];

    auto msg = new QMessageBox(this);
    msg->setWindowTitle(name);
    msg->setText(generic->reportHtml(m_corona->screenPool()));

    msg->open();
}

void SettingsDialog::showScreensInformation()
{
    QList<int> assignedScreens;

    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString id = m_model->data(m_model->index(i, IDCOLUMN), Qt::DisplayRole).toString();
        QString name = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();

        Layout::GenericLayout *genericActive= m_corona->layoutsManager()->synchronizer()->layout(o_layoutsOriginalData[id].originalName());
        Layout::GenericLayout *generic = genericActive ? genericActive : m_layouts[id];

        QList<int> vScreens = generic->viewsScreens();

        for (const int scrId : vScreens) {
            if (!assignedScreens.contains(scrId)) {
                assignedScreens << scrId;
            }
        }
    }

    auto msg = new QMessageBox(this);
    msg->setWindowTitle(i18n("Screens Information"));
    msg->setText(m_corona->screenPool()->reportHtml(assignedScreens));

    msg->open();
}

bool SettingsDialog::saveAllChanges()
{
    if (!dataAreAccepted()) {
        return false;
    }

    //! Update universal settings
    Latte::Types::MouseSensitivity sensitivity = static_cast<Latte::Types::MouseSensitivity>(m_mouseSensitivityButtons->checkedId());
    bool autostart = ui->autostartChkBox->isChecked();
    bool badges3DStyle = ui->badges3DStyleChkBox->isChecked();
    bool forwardMetaPress = ui->metaPressChkBox->isChecked();
    bool metaPressAndHold = ui->metaPressHoldChkBox->isChecked();
    bool showInfoWindow = ui->infoWindowChkBox->isChecked();
    bool noBordersForMaximized = ui->noBordersForMaximizedChkBox->isChecked();

    m_corona->universalSettings()->setMouseSensitivity(sensitivity);
    m_corona->universalSettings()->setAutostart(autostart);
    m_corona->universalSettings()->setBadges3DStyle(badges3DStyle);
    m_corona->universalSettings()->kwin_forwardMetaToLatte(forwardMetaPress);
    m_corona->universalSettings()->setMetaPressAndHoldEnabled(metaPressAndHold);
    m_corona->universalSettings()->setShowInfoWindow(showInfoWindow);
    m_corona->universalSettings()->setCanDisableBorders(noBordersForMaximized);
    m_corona->universalSettings()->setScreenTrackerInterval(ui->screenTrackerSpinBox->value());

    m_corona->themeExtended()->setOutlineWidth(ui->outlineSpinBox->value());

    //! Update Layouts
    QStringList knownActivities = m_corona->layoutsManager()->synchronizer()->activities();

    QTemporaryDir layoutTempDir;

    qDebug() << "Temporary Directory ::: " << layoutTempDir.path();

    QStringList fromRenamePaths;
    QStringList toRenamePaths;
    QStringList toRenameNames;

    QString switchToLayout;

    QHash<QString, Layout::GenericLayout *> activeLayoutsToRename;

    Settings::Data::LayoutsTable removedLayouts = o_layoutsOriginalData.subtracted(m_model->currentData());

    //! remove layouts that have been removed from the user
    for (int i=0; i<removedLayouts.rowCount(); ++i) {
        QFile(removedLayouts[i].id).remove();

        if (m_layouts.contains(removedLayouts[i].id)) {
            CentralLayout *removedLayout = m_layouts.take(removedLayouts[i].id);
            delete removedLayout;
        }
    }

    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString id = m_model->data(m_model->index(i, IDCOLUMN), Qt::DisplayRole).toString();
        QString color = m_model->data(m_model->index(i, COLORCOLUMN), Qt::BackgroundRole).toString();
        QString textColor = m_model->data(m_model->index(i, COLORCOLUMN), Qt::UserRole).toString();
        QString name = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();
        bool locked = m_model->data(m_model->index(i, NAMECOLUMN), Qt::UserRole).toBool();
        bool menu = m_model->data(m_model->index(i, MENUCOLUMN), Qt::DisplayRole).toString() == CheckMark;
        bool disabledBorders = m_model->data(m_model->index(i, BORDERSCOLUMN), Qt::DisplayRole).toString() == CheckMark;
        QStringList lActivities = m_model->data(m_model->index(i, ACTIVITYCOLUMN), Qt::UserRole).toStringList();

        QStringList cleanedActivities;

        //!update only activities that are valid
        for (const auto &activity : lActivities) {
            if (knownActivities.contains(activity) && activity != Settings::Model::Layouts::FREEACTIVITIESID) {
                cleanedActivities.append(activity);
            }
        }

        //qDebug() << i << ". " << id << " - " << color << " - " << name << " - " << menu << " - " << lActivities;
        //! update the generic parts of the layouts
        bool isOriginalLayout = o_layoutsOriginalData.contains(id);
        Layout::GenericLayout *genericActive= isOriginalLayout ?m_corona->layoutsManager()->synchronizer()->layout(o_layoutsOriginalData[id].originalName()) : nullptr;
        Layout::GenericLayout *generic = genericActive ? genericActive : m_layouts[id];

        //! unlock read-only layout
        if (!generic->isWritable()) {
            generic->unlock();
        }

        if (color.startsWith("/")) {
            //it is image file in such case
            if (color != generic->background()) {
                generic->setBackground(color);
            }

            if (generic->textColor() != textColor) {
                generic->setTextColor(textColor);
            }
        } else {
            if (color != generic->color()) {
                generic->setColor(color);
                generic->setBackground(QString());
                generic->setTextColor(QString());
            }
        }

        //! update only the Central-specific layout parts
        CentralLayout *centralActive = isOriginalLayout ? m_corona->layoutsManager()->synchronizer()->centralLayout(o_layoutsOriginalData[id].originalName()) : nullptr;
        CentralLayout *central = centralActive ? centralActive : m_layouts[id];

        if (central->showInMenu() != menu) {
            central->setShowInMenu(menu);
        }

        if (central->disableBordersForMaximizedWindows() != disabledBorders) {
            central->setDisableBordersForMaximizedWindows(disabledBorders);
        }

        if (central->activities() != cleanedActivities) {
            central->setActivities(cleanedActivities);
        }

        //! If the layout name changed OR the layout path is a temporary one
        if (generic->name() != name || (id.startsWith("/tmp/"))) {
            //! If the layout is Active in MultipleLayouts
            if (m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts && generic->isActive()) {
                qDebug() << " Active Layout Should Be Renamed From : " << generic->name() << " TO :: " << name;
                activeLayoutsToRename[name] = generic;
            }

            QString tempFile = layoutTempDir.path() + "/" + QString(generic->name() + ".layout.latte");
            qDebug() << "new temp file ::: " << tempFile;

            if ((m_corona->layoutsManager()->memoryUsage() == Types::SingleLayout) && (generic->name() == m_corona->layoutsManager()->currentLayoutName())) {
                switchToLayout = name;
            }

            generic = m_layouts.take(id);
            delete generic;

            QFile(id).rename(tempFile);

            fromRenamePaths.append(id);
            toRenamePaths.append(tempFile);
            toRenameNames.append(name);
        }
    }

    //! this is necessary in case two layouts have to swap names
    //! so we copy first the layouts in a temp directory and afterwards all
    //! together we move them in the official layout directory
    for (int i = 0; i < toRenamePaths.count(); ++i) {
        QString newFile = QDir::homePath() + "/.config/latte/" + toRenameNames[i] + ".layout.latte";
        QFile(toRenamePaths[i]).rename(newFile);

        CentralLayout *nLayout = new CentralLayout(this, newFile);
        m_layouts[newFile] = nLayout;

        //! updating the #SETTINGSID in the model for the layout that was renamed
        for (int j = 0; j < m_model->rowCount(); ++j) {
            QString tId = m_model->data(m_model->index(j, IDCOLUMN), Qt::DisplayRole).toString();

            if (tId == fromRenamePaths[i]) {
                m_model->setData(m_model->index(j, IDCOLUMN), newFile, Qt::DisplayRole);
            }
        }
    }

    QString orphanedLayout;

    if (m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts) {
        for (const auto &newLayoutName : activeLayoutsToRename.keys()) {
            Layout::GenericLayout *layout = activeLayoutsToRename[newLayoutName];
            qDebug() << " Active Layout of Type: " << layout->type() << " Is Renamed From : " << activeLayoutsToRename[newLayoutName]->name() << " TO :: " << newLayoutName;
            layout->renameLayout(newLayoutName);

            if (layout->type() == Layout::Type::Central) {
                CentralLayout *central = qobject_cast<CentralLayout *>(layout);

                if (central->activities().isEmpty()) {
                    //! that means it is an active layout for orphaned Activities
                    orphanedLayout = newLayoutName;
                }
            }
        }
    }

    //! lock layouts in the end when the user has chosen it
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString id = m_model->data(m_model->index(i, IDCOLUMN), Qt::DisplayRole).toString();
        QString name = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();
        bool locked = m_model->data(m_model->index(i, NAMECOLUMN), Qt::UserRole).toBool();

        bool isOriginalLayout{o_layoutsOriginalData.contains(id)};
        Layout::GenericLayout *generic = isOriginalLayout ? m_corona->layoutsManager()->synchronizer()->layout(o_layoutsOriginalData[id].originalName()) : nullptr;
        Layout::GenericLayout *layout = generic ? generic : m_layouts[id];

        if (layout && locked && layout->isWritable()) {
            layout->lock();
        }
    }

    //! update SharedLayouts that are Active
    syncActiveShares();

    //! reload layouts in layoutsmanager
    m_corona->layoutsManager()->synchronizer()->loadLayouts();

    //! send to layout manager in which layout to switch
    Latte::Types::LayoutsMemoryUsage inMemoryOption = static_cast<Latte::Types::LayoutsMemoryUsage>(m_inMemoryButtons->checkedId());

    if (m_corona->layoutsManager()->memoryUsage() != inMemoryOption) {
        Types::LayoutsMemoryUsage previousMemoryUsage = m_corona->layoutsManager()->memoryUsage();
        m_corona->layoutsManager()->setMemoryUsage(inMemoryOption);

        QVariant value = m_model->data(m_model->index(ui->layoutsView->currentIndex().row(), NAMECOLUMN), Qt::DisplayRole);
        QString layoutName = value.toString();

        m_corona->layoutsManager()->switchToLayout(layoutName, previousMemoryUsage);
    } else {
        if (!switchToLayout.isEmpty()) {
            m_corona->layoutsManager()->switchToLayout(switchToLayout);
        } else if (m_corona->layoutsManager()->memoryUsage() == Types::MultipleLayouts) {
            m_corona->layoutsManager()->synchronizer()->syncMultipleLayoutsToActivities(orphanedLayout);
        }
    }

    return true;
}

void SettingsDialog::syncActiveShares()
{
    if (m_corona->layoutsManager()->memoryUsage() != Types::MultipleLayouts) {
        return;
    }

    Settings::Data::LayoutsTable currentLayoutsData = m_model->currentData();

    Layouts::SharesMap  currentSharesNamesMap = currentLayoutsData.sharesMap();
    QStringList originalSharesIds = o_layoutsOriginalData.allSharesIds();
    QStringList currentSharesIds = currentLayoutsData.allSharesIds();

    QStringList deprecatedSharesIds = Latte::subtracted(originalSharesIds, currentSharesIds);
    QStringList deprecatedSharesNames;

    for(int i=0; i<deprecatedSharesIds.count(); ++i) {
        QString shareId = deprecatedSharesIds[i];

        if (currentLayoutsData.contains(shareId)) {
            deprecatedSharesNames << currentLayoutsData[shareId].editedName();
        } else if (o_layoutsOriginalData.contains(shareId)) {
            deprecatedSharesNames << o_layoutsOriginalData[shareId].editedName();
        }
    }

    qDebug() << " CURRENT SHARES NAMES MAP  :: " << currentSharesNamesMap;
    qDebug() << " DEPRECATED SHARES ::";

    m_corona->layoutsManager()->synchronizer()->syncActiveShares(currentSharesNamesMap, deprecatedSharesNames);
}

bool SettingsDialog::idExistsInModel(QString id)
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString rowId = m_model->data(m_model->index(i, IDCOLUMN), Qt::DisplayRole).toString();

        if (rowId == id) {
            return true;
        }
    }

    return false;
}

bool SettingsDialog::nameExistsInModel(QString name)
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString rowName = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();

        if (rowName == name) {
            return true;
        }
    }

    return false;
}

bool SettingsDialog::inMultipleLayoutsLook() const
{
    Latte::Types::LayoutsMemoryUsage inMemoryOption = static_cast<Latte::Types::LayoutsMemoryUsage>(m_inMemoryButtons->checkedId());
    return inMemoryOption == Latte::Types::MultipleLayouts;
}

bool SettingsDialog::isActive(int row) const
{
    QString id = m_model->data(m_model->index(row, IDCOLUMN), Qt::DisplayRole).toString();
    if (o_layoutsOriginalData.contains(id)){
        return (m_corona->layoutsManager()->synchronizer()->layout(o_layoutsOriginalData[id].originalName()) != nullptr);
    }

    return false;
}

bool SettingsDialog::isActive(QString layoutName) const
{
    return (m_corona->layoutsManager()->synchronizer()->layout(layoutName) != nullptr);
}

bool SettingsDialog::isMenuCell(int column) const
{
    return column == MENUCOLUMN;
}

bool SettingsDialog::isShared(int row) const
{
    if (row >=0 ) {
        QStringList shares = m_model->data(m_model->index(row, SHAREDCOLUMN), Qt::UserRole).toStringList();
        if (!shares.isEmpty()) {
            return true;
        }
    }

    return false;
}

int SettingsDialog::ascendingRowFor(QString name)
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString rowName = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();

        if (rowName.toUpper() > name.toUpper()) {
            return i;
        }
    }

    return m_model->rowCount();
}

int SettingsDialog::rowForId(QString id) const
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString rowId = m_model->data(m_model->index(i, IDCOLUMN), Qt::DisplayRole).toString();

        if (rowId == id) {
            return i;
        }
    }

    return -1;
}

int SettingsDialog::rowForName(QString layoutName) const
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QString rowName = m_model->data(m_model->index(i, NAMECOLUMN), Qt::DisplayRole).toString();

        if (rowName == layoutName) {
            return i;
        }
    }

    return -1;
}

QString SettingsDialog::idForRow(int row) const
{
    return m_model->data(m_model->index(row, IDCOLUMN), Qt::DisplayRole).toString();
}

QString SettingsDialog::nameForId(QString id) const
{
    int row = rowForId(id);
    return m_model->data(m_model->index(row, NAMECOLUMN), Qt::DisplayRole).toString();
}

QString SettingsDialog::uniqueTempDirectory()
{
    QTemporaryDir tempDir;
    tempDir.setAutoRemove(false);
    m_tempDirectories.append(tempDir.path());

    return tempDir.path();
}

QString SettingsDialog::uniqueLayoutName(QString name)
{
    int pos_ = name.lastIndexOf(QRegExp(QString("[-][0-9]+")));

    if (nameExistsInModel(name) && pos_ > 0) {
        name = name.left(pos_);
    }

    int i = 2;

    QString namePart = name;

    while (nameExistsInModel(name)) {
        name = namePart + "-" + QString::number(i);
        i++;
    }

    return name;
}

}//end of namespace

