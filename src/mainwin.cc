/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <iostream>
#include "comment.h"
#include "openscad.h"
#include "GeometryCache.h"
#include "ModuleCache.h"
#include "MainWindow.h"
#include "OpenSCADApp.h"
#include "parsersettings.h"
#include "rendersettings.h"
#include "Preferences.h"
#include "printutils.h"
#include "node.h"
#include "polyset.h"
#include "csgnode.h"
#include "highlighter.h"
#include "builtin.h"
#include "memory.h"
#include "expression.h"
#include "modcontext.h"
#include "progress.h"
#include "dxfdim.h"
#include "settings.h"
#include "AboutDialog.h"
#include "FontListDialog.h"
#include "LibraryInfoDialog.h"
#ifdef ENABLE_OPENCSG
#include "CSGTreeEvaluator.h"
#include "OpenCSGRenderer.h"
#include <opencsg.h>
#endif
#include "ProgressWidget.h"
#include "ThrownTogetherRenderer.h"
#include "CSGTreeNormalizer.h"
#include "QGLView.h"
#ifdef Q_OS_MAC
#include "CocoaUtils.h"
#endif
#include "PlatformUtils.h"
#ifdef OPENSCAD_UPDATER
#include "AutoUpdater.h"
#endif
#include "tabmanager.h"

#include <QMenu>
#include <QTime>
#include <QMenuBar>
#include <QSplitter>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QTextStream>
#include <QStatusBar>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QTimer>
#include <QMessageBox>
#include <QDesktopServices>
#include <QProgressDialog>
#include <QMutexLocker>
#include <QTemporaryFile>
#include <QDockWidget>
#include <QClipboard>
#include <QDesktopWidget>
#include <string>
#include "QWordSearchField.h"
#include <QSettings> //Include QSettings for direct operations on settings arrays
#include "QSettingsCached.h"
#include <QSound>

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#include <QTextDocument>
#define QT_HTML_ESCAPE(qstring) Qt::escape(qstring)
#undef ENABLE_3D_PRINTING
#else
#define QT_HTML_ESCAPE(qstring) (qstring).toHtmlEscaped()
#define ENABLE_3D_PRINTING
#include "OctoPrint.h"
#include "PrintService.h"
#endif

#include <fstream>

#include <algorithm>
#include <boost/version.hpp>
#include <sys/stat.h>

#ifdef ENABLE_CGAL

#include "CGALCache.h"
#include "GeometryEvaluator.h"
#include "CGALRenderer.h"
#include "CGAL_Nef_polyhedron.h"
#include "cgal.h"
#include "cgalworker.h"
#include "cgalutils.h"

#endif // ENABLE_CGAL

#include "FontCache.h"
#include "PrintInitDialog.h"
#include "input/InputDriverManager.h"
#include <cstdio>
#include <QtNetwork>

// Global application state
unsigned int GuiLocker::gui_locked = 0;

static char copyrighttext[] =
	"Copyright (C) 2009-2019 The OpenSCAD Developers\n\n"
	"This program is free software; you can redistribute it and/or modify "
	"it under the terms of the GNU General Public License as published by "
	"the Free Software Foundation; either version 2 of the License, or "
	"(at your option) any later version.\n";
bool MainWindow::undockMode = false;
bool MainWindow::reorderMode = false;
const int MainWindow::tabStopWidth = 15;
QElapsedTimer *MainWindow::progressThrottle = new QElapsedTimer();

namespace {

QAction *findAction(const QList<QAction *> &actions, const std::string &name)
{
	for (const auto action : actions) {
		if (action->objectName().toStdString() == name) {
			return action;
		}
		if (action->menu()) {
			auto foundAction = findAction(action->menu()->actions(), name);
			if (foundAction) return foundAction;
		}
   }
   return nullptr;
}

void fileExportedMessage(const char *format, const QString &filename) {
	PRINTB("%s export finished: %s", format % filename.toUtf8().constData());
}

} // namespace

MainWindow::MainWindow(const QStringList &filenames)
	: root_inst("group"), library_info_dialog(nullptr), font_list_dialog(nullptr), procevents(false), tempFile(nullptr),
      progresswidget(nullptr), includes_mtime(0), deps_mtime(0), last_parser_error_pos(-1)
{
	setupUi(this);

	editorDockTitleWidget = new QWidget();
	consoleDockTitleWidget = new QWidget();
	parameterDockTitleWidget = new QWidget();

	this->editorDock->setConfigKey("view/hideEditor");
	this->editorDock->setAction(this->viewActionHideEditor);
	this->consoleDock->setConfigKey("view/hideConsole");
	this->consoleDock->setAction(this->viewActionHideConsole);
	this->parameterDock->setConfigKey("view/hideCustomizer");
	this->parameterDock->setAction(this->viewActionHideParameters);

	this->versionLabel = nullptr; // must be initialized before calling updateStatusBar()
	updateStatusBar(nullptr);

	const QString importStatement = "import(\"%1\");\n";
	const QString surfaceStatement = "surface(\"%1\");\n";
	knownFileExtensions["stl"] = importStatement;
	knownFileExtensions["3mf"] = importStatement;
	knownFileExtensions["off"] = importStatement;
	knownFileExtensions["dxf"] = importStatement;
	knownFileExtensions["svg"] = importStatement;
	knownFileExtensions["amf"] = importStatement;
	knownFileExtensions["dat"] = surfaceStatement;
	knownFileExtensions["png"] = surfaceStatement;
	knownFileExtensions["scad"] = "";
	knownFileExtensions["csg"] = "";

	root_module = nullptr;
	parsed_module = nullptr;
	absolute_root_node = nullptr;

	// Open Recent
	for (int i = 0;i<UIUtils::maxRecentFiles; i++) {
		this->actionRecentFile[i] = new QAction(this);
		this->actionRecentFile[i]->setVisible(false);
		this->menuOpenRecent->addAction(this->actionRecentFile[i]);
		connect(this->actionRecentFile[i], SIGNAL(triggered()),
						this, SLOT(actionOpenRecent()));
	}

	tabManager = new TabManager(this, filenames.isEmpty() ? QString() : filenames[0]);
	connect(tabManager, SIGNAL(tabCountChanged(int)), this, SLOT(setTabToolBarVisible(int)));
	this->setTabToolBarVisible(tabManager->count());
	tabToolBarContents->layout()->addWidget(tabManager->getTabHeader());
	editorDockContents->layout()->addWidget(tabManager->getTabContent());

    connect(Preferences::inst()->ButtonConfig, SIGNAL(inputMappingChanged()), InputDriverManager::instance(), SLOT(onInputMappingUpdated()), Qt::UniqueConnection);
    connect(Preferences::inst()->AxisConfig, SIGNAL(inputMappingChanged()), InputDriverManager::instance(), SLOT(onInputMappingUpdated()), Qt::UniqueConnection);
    connect(Preferences::inst()->AxisConfig, SIGNAL(inputCalibrationChanged()), InputDriverManager::instance(), SLOT(onInputCalibrationUpdated()), Qt::UniqueConnection);
    connect(Preferences::inst()->AxisConfig, SIGNAL(inputGainChanged()), InputDriverManager::instance(), SLOT(onInputGainUpdated()), Qt::UniqueConnection);

	setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
	setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

	this->setAttribute(Qt::WA_DeleteOnClose);

	scadApp->windowManager.add(this);

#ifdef ENABLE_CGAL
	this->cgalworker = new CGALWorker();
	connect(this->cgalworker, SIGNAL(done(shared_ptr<const Geometry>)),
					this, SLOT(actionRenderDone(shared_ptr<const Geometry>)));
#endif

#ifdef ENABLE_CGAL
	this->cgalRenderer = nullptr;
#endif
#ifdef ENABLE_OPENCSG
	this->opencsgRenderer = nullptr;
#endif
	this->thrownTogetherRenderer = nullptr;

	root_node = nullptr;

	this->anim_step = 0;
	this->anim_numsteps = 0;
	this->anim_tval = 0.0;
	this->anim_dumping = false;
	this->anim_dump_start_step = 0;

	editActionZoomTextIn->setShortcuts(QList<QKeySequence>() << editActionZoomTextIn->shortcuts() << QKeySequence("CTRL+="));

	this->qglview->statusLabel = new QLabel(this);
	this->qglview->statusLabel->setMinimumWidth(100);
	statusBar()->addWidget(this->qglview->statusLabel);

	QSettingsCached settings;
	auto s = Settings::Settings::inst();
	this->qglview->setMouseCentricZoom(s->get(Settings::Settings::mouseCentricZoom).toBool());

	animate_timer = new QTimer(this);
	connect(animate_timer, SIGNAL(timeout()), this, SLOT(updateTVal()));

	autoReloadTimer = new QTimer(this);
	autoReloadTimer->setSingleShot(false);
	autoReloadTimer->setInterval(200);
	connect(autoReloadTimer, SIGNAL(timeout()), this, SLOT(checkAutoReload()));

	waitAfterReloadTimer = new QTimer(this);
	waitAfterReloadTimer->setSingleShot(true);
	waitAfterReloadTimer->setInterval(200);
	connect(waitAfterReloadTimer, SIGNAL(timeout()), this, SLOT(waitAfterReload()));
	connect(this->parameterWidget, SIGNAL(previewRequested(bool)), this, SLOT(actionRenderPreview(bool)));
	connect(Preferences::inst(), SIGNAL(ExperimentalChanged()), this, SLOT(changeParameterWidget()));
	connect(this->e_tval, SIGNAL(textChanged(QString)), this, SLOT(updatedAnimTval()));
	connect(this->e_fps, SIGNAL(textChanged(QString)), this, SLOT(updatedAnimFps()));
	connect(this->e_fsteps, SIGNAL(textChanged(QString)), this, SLOT(updatedAnimSteps()));
	connect(this->e_dump, SIGNAL(toggled(bool)), this, SLOT(updatedAnimDump(bool)));

	progressThrottle->start();

	animate_panel->hide();
	this->hideFind(); 
	frameCompileResult->hide();
	this->labelCompileResultMessage->setOpenExternalLinks(false);
	connect(this->labelCompileResultMessage, SIGNAL(linkActivated(QString)), SLOT(showConsole()));

	// File menu
	connect(this->fileActionNewWindow, SIGNAL(triggered()), this, SLOT(actionNewWindow()));
	connect(this->fileActionNew, SIGNAL(triggered()), tabManager, SLOT(actionNew()));
	connect(this->fileActionOpenWindow, SIGNAL(triggered()), this, SLOT(actionOpenWindow()));
	connect(this->fileActionOpen, SIGNAL(triggered()), this, SLOT(actionOpen()));
	connect(this->fileActionSave, SIGNAL(triggered()), this, SLOT(actionSave()));
	connect(this->fileActionSaveAs, SIGNAL(triggered()), this, SLOT(actionSaveAs()));
	connect(this->fileActionSaveAll, SIGNAL(triggered()), tabManager, SLOT(saveAll()));
	connect(this->fileActionReload, SIGNAL(triggered()), this, SLOT(actionReload()));
	connect(this->fileActionQuit, SIGNAL(triggered()), this, SLOT(quit()));
	connect(this->fileShowLibraryFolder, SIGNAL(triggered()), this, SLOT(actionShowLibraryFolder()));
#ifndef __APPLE__
	auto shortcuts = this->fileActionSave->shortcuts();
	shortcuts.push_back(QKeySequence(Qt::Key_F2));
	this->fileActionSave->setShortcuts(shortcuts);
	shortcuts = this->fileActionReload->shortcuts();
	shortcuts.push_back(QKeySequence(Qt::Key_F3));
	this->fileActionReload->setShortcuts(shortcuts);
#endif

	this->menuOpenRecent->addSeparator();
	this->menuOpenRecent->addAction(this->fileActionClearRecent);
	connect(this->fileActionClearRecent, SIGNAL(triggered()),
					this, SLOT(clearRecentFiles()));

	show_examples();

	connect(this->editActionCopyViewport, SIGNAL(triggered()), this, SLOT(actionCopyViewport()));
	connect(this->editActionConvertTabsToSpaces, SIGNAL(triggered()), this, SLOT(convertTabsToSpaces()));
	connect(this->editActionCopyVPT, SIGNAL(triggered()), this, SLOT(copyViewportTranslation()));
	connect(this->editActionCopyVPR, SIGNAL(triggered()), this, SLOT(copyViewportRotation()));
	connect(this->editActionCopyVPD, SIGNAL(triggered()), this, SLOT(copyViewportDistance()));
	connect(this->editActionPreferences, SIGNAL(triggered()), this, SLOT(preferences()));
    // Edit->Find
    connect(this->editActionFind, SIGNAL(triggered()), this, SLOT(showFind()));
    connect(this->editActionFindAndReplace, SIGNAL(triggered()), this, SLOT(showFindAndReplace()));
#ifdef Q_OS_WIN
	this->editActionFindAndReplace->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_F));
#endif
	connect(this->editActionFindNext, SIGNAL(triggered()), this, SLOT(findNext()));
	connect(this->editActionFindPrevious, SIGNAL(triggered()), this, SLOT(findPrev()));
	connect(this->editActionUseSelectionForFind, SIGNAL(triggered()), this, SLOT(useSelectionForFind()));

	// Design menu
	connect(this->designActionAutoReload, SIGNAL(toggled(bool)), this, SLOT(autoReloadSet(bool)));
	connect(this->designActionReloadAndPreview, SIGNAL(triggered()), this, SLOT(actionReloadRenderPreview()));
	connect(this->designActionPreview, SIGNAL(triggered()), this, SLOT(actionRenderPreview()));
#ifdef ENABLE_CGAL
	connect(this->designActionRender, SIGNAL(triggered()), this, SLOT(actionRender()));
#else
	this->designActionRender->setVisible(false);
#endif
	connect(this->designAction3DPrint, SIGNAL(triggered()), this, SLOT(action3DPrint()));
	connect(this->designCheckValidity, SIGNAL(triggered()), this, SLOT(actionCheckValidity()));
	connect(this->designActionDisplayAST, SIGNAL(triggered()), this, SLOT(actionDisplayAST()));
	connect(this->designActionDisplayCSGTree, SIGNAL(triggered()), this, SLOT(actionDisplayCSGTree()));
	connect(this->designActionDisplayCSGProducts, SIGNAL(triggered()), this, SLOT(actionDisplayCSGProducts()));
	connect(this->fileActionExportSTL, SIGNAL(triggered()), this, SLOT(actionExportSTL()));
	connect(this->fileActionExport3MF, SIGNAL(triggered()), this, SLOT(actionExport3MF()));
	connect(this->fileActionExportOFF, SIGNAL(triggered()), this, SLOT(actionExportOFF()));
	connect(this->fileActionExportAMF, SIGNAL(triggered()), this, SLOT(actionExportAMF()));
	connect(this->fileActionExportDXF, SIGNAL(triggered()), this, SLOT(actionExportDXF()));
	connect(this->fileActionExportSVG, SIGNAL(triggered()), this, SLOT(actionExportSVG()));
	connect(this->fileActionExportCSG, SIGNAL(triggered()), this, SLOT(actionExportCSG()));
	connect(this->fileActionExportImage, SIGNAL(triggered()), this, SLOT(actionExportImage()));
	connect(this->designActionFlushCaches, SIGNAL(triggered()), this, SLOT(actionFlushCaches()));

#ifndef ENABLE_LIB3MF
	this->fileActionExport3MF->setVisible(false);
#endif

#ifndef ENABLE_3D_PRINTING
	this->designAction3DPrint->setVisible(false);
	this->designAction3DPrint->setEnabled(false);
#endif

	// View menu
#ifndef ENABLE_OPENCSG
	this->viewActionPreview->setVisible(false);
#else
	connect(this->viewActionPreview, SIGNAL(triggered()), this, SLOT(viewModePreview()));
	if (!this->qglview->hasOpenCSGSupport()) {
		this->viewActionPreview->setEnabled(false);
	}
#endif

#ifdef ENABLE_CGAL
	connect(this->viewActionSurfaces, SIGNAL(triggered()), this, SLOT(viewModeSurface()));
	connect(this->viewActionWireframe, SIGNAL(triggered()), this, SLOT(viewModeWireframe()));
#else
	this->viewActionSurfaces->setVisible(false);
	this->viewActionWireframe->setVisible(false);
#endif
	connect(this->viewActionThrownTogether, SIGNAL(triggered()), this, SLOT(viewModeThrownTogether()));
	connect(this->viewActionShowEdges, SIGNAL(triggered()), this, SLOT(viewModeShowEdges()));
	connect(this->viewActionShowAxes, SIGNAL(triggered()), this, SLOT(viewModeShowAxes()));
	connect(this->viewActionShowCrosshairs, SIGNAL(triggered()), this, SLOT(viewModeShowCrosshairs()));
	connect(this->viewActionShowScaleProportional, SIGNAL(triggered()), this, SLOT(viewModeShowScaleProportional()));
	connect(this->viewActionAnimate, SIGNAL(triggered()), this, SLOT(viewModeAnimate()));
	connect(this->viewActionTop, SIGNAL(triggered()), this, SLOT(viewAngleTop()));
	connect(this->viewActionBottom, SIGNAL(triggered()), this, SLOT(viewAngleBottom()));
	connect(this->viewActionLeft, SIGNAL(triggered()), this, SLOT(viewAngleLeft()));
	connect(this->viewActionRight, SIGNAL(triggered()), this, SLOT(viewAngleRight()));
	connect(this->viewActionFront, SIGNAL(triggered()), this, SLOT(viewAngleFront()));
	connect(this->viewActionBack, SIGNAL(triggered()), this, SLOT(viewAngleBack()));
	connect(this->viewActionDiagonal, SIGNAL(triggered()), this, SLOT(viewAngleDiagonal()));
	connect(this->viewActionCenter, SIGNAL(triggered()), this, SLOT(viewCenter()));
	connect(this->viewActionResetView, SIGNAL(triggered()), this, SLOT(viewResetView()));
	connect(this->viewActionViewAll, SIGNAL(triggered()), this, SLOT(viewAll()));
	connect(this->viewActionPerspective, SIGNAL(triggered()), this, SLOT(viewPerspective()));
	connect(this->viewActionOrthogonal, SIGNAL(triggered()), this, SLOT(viewOrthogonal()));
	connect(this->viewActionZoomIn, SIGNAL(triggered()), qglview, SLOT(ZoomIn()));
	connect(this->viewActionZoomOut, SIGNAL(triggered()), qglview, SLOT(ZoomOut()));
	connect(this->viewActionHideToolBars, SIGNAL(triggered()), this, SLOT(hideToolbars()));
	connect(this->viewActionHideEditor, SIGNAL(triggered()), this, SLOT(hideEditor()));
	connect(this->viewActionHideConsole, SIGNAL(triggered()), this, SLOT(hideConsole()));
    connect(this->viewActionHideParameters, SIGNAL(triggered()), this, SLOT(hideParameters()));
	// Help menu
	connect(this->helpActionAbout, SIGNAL(triggered()), this, SLOT(helpAbout()));
	connect(this->helpActionHomepage, SIGNAL(triggered()), this, SLOT(helpHomepage()));
	connect(this->helpActionManual, SIGNAL(triggered()), this, SLOT(helpManual()));
	connect(this->helpActionCheatSheet, SIGNAL(triggered()), this, SLOT(helpCheatSheet()));
	connect(this->helpActionLibraryInfo, SIGNAL(triggered()), this, SLOT(helpLibrary()));
	connect(this->helpActionFontInfo, SIGNAL(triggered()), this, SLOT(helpFontInfo()));

#ifdef OPENSCAD_UPDATER
	this->menuBar()->addMenu(AutoUpdater::updater()->updateMenu);
#endif

	setCurrentOutput();

	std::string helptitle = "OpenSCAD " + openscad_versionnumber +  "\nhttps://www.openscad.org/\n";
	PRINT(helptitle);
	PRINT(copyrighttext);

	connect(this->qglview, SIGNAL(doAnimateUpdate()), this, SLOT(animateUpdate()));

	connect(Preferences::inst(), SIGNAL(requestRedraw()), this->qglview, SLOT(updateGL()));
	connect(Preferences::inst(), SIGNAL(updateMouseCentricZoom(bool)), this->qglview, SLOT(setMouseCentricZoom(bool)));
	connect(Preferences::inst(), SIGNAL(updateReorderMode(bool)), this, SLOT(updateReorderMode(bool)));
	connect(Preferences::inst(), SIGNAL(updateUndockMode(bool)), this, SLOT(updateUndockMode(bool)));
	connect(Preferences::inst(), SIGNAL(openCSGSettingsChanged()),
					this, SLOT(openCSGSettingsChanged()));
	connect(Preferences::inst(), SIGNAL(colorSchemeChanged(const QString&)),
					this, SLOT(setColorScheme(const QString&)));

	Preferences::inst()->apply_win(); // not sure if to be commented, checked must not be commented(done some changes in apply())

	QString cs = Preferences::inst()->getValue("3dview/colorscheme").toString();
	this->setColorScheme(cs);

	//find and replace panel
	connect(this->findTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(selectFindType(int)));
	connect(this->findInputField, SIGNAL(textChanged(QString)), this, SLOT(findString(QString)));
	connect(this->findInputField, SIGNAL(returnPressed()), this->findNextButton, SLOT(animateClick()));
	find_panel->installEventFilter(this);
	if (QApplication::clipboard()->supportsFindBuffer()) {
		connect(this->findInputField, SIGNAL(textChanged(QString)), this, SLOT(updateFindBuffer(QString)));
		connect(QApplication::clipboard(), SIGNAL(findBufferChanged()), this, SLOT(findBufferChanged()));
		// With Qt 4.8.6, there seems to be a bug that often gives an incorrect findbuffer content when
		// the app receives focus for the first time
		this->findInputField->setText(QApplication::clipboard()->text(QClipboard::FindBuffer));
	}

	connect(this->findPrevButton, SIGNAL(clicked()), this, SLOT(findPrev()));
	connect(this->findNextButton, SIGNAL(clicked()), this, SLOT(findNext()));
	connect(this->cancelButton, SIGNAL(clicked()), this, SLOT(hideFind()));
	connect(this->replaceButton, SIGNAL(clicked()), this, SLOT(replace()));
	connect(this->replaceAllButton, SIGNAL(clicked()), this, SLOT(replaceAll()));
	connect(this->replaceInputField, SIGNAL(returnPressed()), this->replaceButton, SLOT(animateClick()));

	addKeyboardShortCut(this->viewerToolBar->actions());
	addKeyboardShortCut(this->editortoolbar->actions());

	InputDriverManager::instance()->registerActions(this->menuBar()->actions(),"");
	Preferences* instance = Preferences::inst();
	instance->ButtonConfig->init();

	initActionIcon(fileActionNew, ":/images/blackNew.png", ":/images/Document-New-128.png");
	initActionIcon(fileActionOpen, ":/images/Open-32.png", ":/images/Open-128.png");
	initActionIcon(fileActionSave, ":/images/Save-32.png", ":/images/Save-128.png");
	initActionIcon(editActionZoomTextIn, ":/images/zoom-text-in.png", ":/images/zoom-text-in-white.png");
	initActionIcon(editActionZoomTextOut, ":/images/zoom-text-out.png", ":/images/zoom-text-out-white.png");
	initActionIcon(designActionRender, ":/images/render-32.png", ":/images/render-32-white.png");
	initActionIcon(designAction3DPrint, ":/images/3dprint-32.png", ":/images/3dprint-32-white.png");
	initActionIcon(viewActionShowAxes, ":/images/blackaxes.png", ":/images/axes.png");
	initActionIcon(viewActionShowEdges, ":/images/Rotation-32.png", ":/images/grid.png");
	initActionIcon(viewActionZoomIn, ":/images/zoomin.png", ":/images/Zoom-In-32.png");
	initActionIcon(viewActionZoomOut, ":/images/zoomout.png", ":/images/Zoom-Out-32.png");
	initActionIcon(viewActionTop, ":/images/blackUp.png", ":/images/up.png");
	initActionIcon(viewActionBottom, ":/images/blackbottom.png", ":/images/bottom.png");
	initActionIcon(viewActionLeft, ":/images/blackleft.png", ":/images/left.png");
	initActionIcon(viewActionRight, ":/images/rightright.png", ":/images/right.png");
	initActionIcon(viewActionFront, ":/images/blackfront.png", ":/images/front.png");
	initActionIcon(viewActionBack, ":/images/blackback.png", ":/images/back.png");
	initActionIcon(viewActionSurfaces, ":/images/surface.png", ":/images/surfaceWhite.png");
	initActionIcon(viewActionWireframe, ":/images/wireframe1.png", ":/images/wireframeWhite.png");
	initActionIcon(viewActionShowCrosshairs, ":/images/cross.png", ":/images/crosswhite.png");
	initActionIcon(viewActionPerspective, ":/images/perspective1.png", ":/images/perspective1white.png");
	initActionIcon(viewActionOrthogonal, ":/images/orthogonal.png", ":/images/orthogonalwhite.png");
	initActionIcon(designActionPreview, ":/images/preview-32.png", ":/images/preview-32-white.png");
	initActionIcon(viewActionAnimate, ":/images/animate.png", ":/images/animate.png");
	initActionIcon(fileActionExportSTL, ":/images/STL.png", ":/images/STL-white.png");
	initActionIcon(fileActionExportAMF, ":/images/AMF.png", ":/images/AMF-white.png");
	initActionIcon(fileActionExport3MF, ":/images/3MF.png", ":/images/3MF-white.png");
	initActionIcon(fileActionExportOFF, ":/images/OFF.png", ":/images/OFF-white.png");
	initActionIcon(fileActionExportDXF, ":/images/DXF.png", ":/images/DXF-white.png");
	initActionIcon(fileActionExportSVG, ":/images/SVG.png", ":/images/SVG-white.png");
	initActionIcon(fileActionExportCSG, ":/images/CSG.png", ":/images/CSG-white.png");
	initActionIcon(fileActionExportImage, ":/images/PNG.png", ":/images/PNG-white.png");
	initActionIcon(viewActionViewAll, ":/images/zoom-all.png", ":/images/zoom-all-white.png");
	initActionIcon(editActionUndo, ":/images/Command-Undo-32.png", ":/images/Command-Undo-32-white.png");
	initActionIcon(editActionRedo, ":/images/Command-Redo-32.png", ":/images/Command-Redo-32-white.png");
	initActionIcon(editActionUnindent, ":/images/Decrease-Indent-32.png", ":/images/Decrease-Indent-32-white.png");
	initActionIcon(editActionIndent, ":/images/Increase-Indent-32.png", ":/images/Increase-Indent-32-white.png");
	initActionIcon(viewActionResetView, ":/images/Command-Reset-32.png", ":/images/Command-Reset-32-white.png");
	initActionIcon(viewActionShowScaleProportional, ":/images/scalemarkers.png", ":/images/scalemarkers-white.png");

	// fetch window states to be restored after restoreState() call
	bool hideConsole = settings.value("view/hideConsole").toBool();
	bool hideEditor = settings.value("view/hideEditor").toBool();
	bool hideCustomizer = settings.value("view/hideCustomizer").toBool();
	bool hideToolbar = settings.value("view/hideToolbar").toBool();
	
	// make sure it looks nice..
	auto windowState = settings.value("window/state", QByteArray()).toByteArray();
	restoreState(windowState);
	resize(settings.value("window/size", QSize(800, 600)).toSize());
	move(settings.value("window/position", QPoint(0, 0)).toPoint());
	updateWindowSettings(hideConsole, hideEditor, hideCustomizer, hideToolbar);

	if (windowState.size() == 0) {
		/*
		 * This triggers only in case the configuration file has no
		 * window state information (or no configuration file at all).
		 * When this happens, the editor would default to a very ugly
		 * width due to the dock widget layout. This overwrites the
		 * value reported via sizeHint() to a width a bit smaller than
		 * half the main window size (either the one loaded from the
		 * configuration or the default value of 800).
		 * The height is only a dummy value which will be essentially
		 * ignored by the layouting as the editor is set to expand to
		 * fill the available space.
		 */
		activeEditor->setInitialSizeHint(QSize((5 * this->width() / 11), 100));
	} else {
#ifdef Q_OS_WIN
		// Try moving the main window into the display range, this
		// can occur when closing OpenSCAD on a second monitor which
		// is not available at the time the application is started
		// again.
		// On Windows that causes the main window to open in a not
		// easily reachable place.
		auto desktop = QApplication::desktop();
		auto desktopRect = desktop->frameGeometry().adjusted(250, 150, -250, -150).normalized();
		auto windowRect = frameGeometry();
		if (!desktopRect.intersects(windowRect)) {
			windowRect.moveCenter(desktopRect.center());
			windowRect = windowRect.intersected(desktopRect);
			move(windowRect.topLeft());
			resize(windowRect.size());
		}
#endif
	}

	connect(this->editorDock, SIGNAL(topLevelChanged(bool)), this, SLOT(editorTopLevelChanged(bool)));
	connect(this->consoleDock, SIGNAL(topLevelChanged(bool)), this, SLOT(consoleTopLevelChanged(bool)));
	connect(this->parameterDock, SIGNAL(topLevelChanged(bool)), this, SLOT(parameterTopLevelChanged(bool)));

	// display this window and check for OpenGL 2.0 (OpenCSG) support
	viewModeThrownTogether();
	show();

#ifdef ENABLE_OPENCSG
	viewModePreview();
#else
	viewModeThrownTogether();
#endif
	loadViewSettings();
	loadDesignSettings();

	setAcceptDrops(true);
	clearCurrentOutput();

	this->console->setMaximumBlockCount(5000);

	for(int i = 1; i < filenames.size(); i++)
		tabManager->createTab(filenames[i]);
}

void MainWindow::initActionIcon(QAction *action, const char *darkResource, const char *lightResource)
{
	int defaultcolor = viewerToolBar->palette().background().color().lightness();
	const char *resource = (defaultcolor > 165) ? darkResource : lightResource;
	action->setIcon(QIcon(resource));
}

void MainWindow::addKeyboardShortCut(const QList<QAction *> &actions)
{
	for (auto &action : actions) {
		// prevent adding shortcut twice if action is added to multiple toolbars
		if (action->toolTip().contains("&nbsp;")) {
	    continue;
		}
		
		const QString shortCut(action->shortcut().toString(QKeySequence::NativeText));
		if (shortCut.isEmpty()) {
	    continue;
		}
		
		const QString toolTip("%1 &nbsp;<span style=\"color: gray; font-size: small; font-style: italic\">%2</span>");
		action->setToolTip(toolTip.arg(action->toolTip(), shortCut));
	}
}

/**
 * Update window settings that get overwritten by the restoreState()
 * Qt call. So the values are loaded before the call and restored here
 * regardless of the (potential outdated) serialized state.
 */
void MainWindow::updateWindowSettings(bool console, bool editor, bool customizer, bool toolbar)
{
	viewActionHideConsole->setChecked(console);
	hideConsole();
	viewActionHideEditor->setChecked(editor);
	hideEditor();
	viewActionHideToolBars->setChecked(toolbar);
	hideToolbars();
	viewActionHideParameters->setChecked(customizer);
	hideParameters();
}

void MainWindow::onAxisChanged(InputEventAxisChanged *)
{

}

void MainWindow::onButtonChanged(InputEventButtonChanged *)
{

}

void MainWindow::onTranslateEvent(InputEventTranslate *event)
{
    double zoomFactor = 0.001 * qglview->cam.zoomValue();
    
    if(event->viewPortRelative){
		qglview->translate(event->x, event->y, event->z, event->relative, true);
	}else{
		qglview->translate(zoomFactor * event->x, event->y, zoomFactor * event->z, event->relative, false);
	}

}

void MainWindow::onRotateEvent(InputEventRotate *event)
{
	qglview->rotate(event->x, event->y, event->z, event->relative);
}

void MainWindow::onRotate2Event(InputEventRotate2 *event)
{
	qglview->rotate2(event->x, event->y, event->z);
}

void MainWindow::onActionEvent(InputEventAction *event)
{
	QAction *action = findAction(this->menuBar()->actions(), event->action);
	if (action) {
		action->trigger();
	}else if("viewActionTogglePerspective" == event->action){
		viewTogglePerspective();
	}
}

void MainWindow::onZoomEvent(InputEventZoom *event)
{
    qglview->zoom(event->zoom, event->relative);
}

void MainWindow::loadViewSettings(){
	QSettingsCached settings;

	if (settings.value("view/showEdges").toBool()) {
		viewActionShowEdges->setChecked(true);
		viewModeShowEdges();
	}
	if (settings.value("view/showAxes", true).toBool()) {
		viewActionShowAxes->setChecked(true);
		viewModeShowAxes();
	}
	if (settings.value("view/showCrosshairs").toBool()) {
		viewActionShowCrosshairs->setChecked(true);
		viewModeShowCrosshairs();
	}
	if (settings.value("view/showScaleProportional", true).toBool()) {
        viewActionShowScaleProportional->setChecked(true);
        viewModeShowScaleProportional();
    }
	if (settings.value("view/orthogonalProjection").toBool()) {
		viewOrthogonal();
	} else {
		viewPerspective();
	}

	updateUndockMode(settings.value("advanced/undockableWindows").toBool());
	updateReorderMode(settings.value("advanced/reorderWindows").toBool());
}

void MainWindow::loadDesignSettings()
{
	QSettingsCached settings;
	if (settings.value("design/autoReload", true).toBool()) {
		designActionAutoReload->setChecked(true);
	}
	auto polySetCacheSizeMB = Preferences::inst()->getValue("advanced/polysetCacheSizeMB").toUInt();
	GeometryCache::instance()->setMaxSizeMB(polySetCacheSizeMB);
#ifdef ENABLE_CGAL
	auto cgalCacheSizeMB = Preferences::inst()->getValue("advanced/cgalCacheSizeMB").toUInt();
	CGALCache::instance()->setMaxSizeMB(cgalCacheSizeMB);
#endif
}

void MainWindow::updateUndockMode(bool undockMode)
{
	MainWindow::undockMode = undockMode;
	if (undockMode) {
		editorDock->setFeatures(editorDock->features() | QDockWidget::DockWidgetFloatable);
		consoleDock->setFeatures(consoleDock->features() | QDockWidget::DockWidgetFloatable);
		parameterDock->setFeatures(parameterDock->features() | QDockWidget::DockWidgetFloatable);
	} else {
		if (editorDock->isFloating()) {
			editorDock->setFloating(false);
		}
		editorDock->setFeatures(editorDock->features() & ~QDockWidget::DockWidgetFloatable);
		if (consoleDock->isFloating()) {
			consoleDock->setFloating(false);
		}
		consoleDock->setFeatures(consoleDock->features() & ~QDockWidget::DockWidgetFloatable);
		if (parameterDock->isFloating()) {
			parameterDock->setFloating(false);
		}
		parameterDock->setFeatures(parameterDock->features() & ~QDockWidget::DockWidgetFloatable);
	}
}

void MainWindow::updateReorderMode(bool reorderMode)
{
	MainWindow::reorderMode = reorderMode;
	editorDock->setTitleBarWidget(reorderMode ? nullptr : editorDockTitleWidget);
	consoleDock->setTitleBarWidget(reorderMode ? nullptr : consoleDockTitleWidget);
	parameterDock->setTitleBarWidget(reorderMode ? nullptr : parameterDockTitleWidget);
}

MainWindow::~MainWindow()
{
	// If root_module is not null then it will be the same as parsed_module,
	// so no need to delete it.
	delete parsed_module;
	delete root_node;
#ifdef ENABLE_CGAL
	this->root_geom.reset();
	delete this->cgalRenderer;
#endif
#ifdef ENABLE_OPENCSG
	delete this->opencsgRenderer;
#endif
	delete this->thrownTogetherRenderer;
	scadApp->windowManager.remove(this);
	if (scadApp->windowManager.getWindows().size() == 0) {
		// Quit application even in case some other windows like
		// Preferences are still open.
		this->quit();
	}
}

void MainWindow::showProgress()
{
	updateStatusBar(qobject_cast<ProgressWidget*>(sender()));
}

void MainWindow::report_func(const class AbstractNode*, void *vp, int mark)
{
	// limit to progress bar update calls to 5 per second
	static const qint64 MIN_TIMEOUT = 200;
	if (progressThrottle->hasExpired(MIN_TIMEOUT)) {
		progressThrottle->start();

		auto thisp = static_cast<MainWindow*>(vp);
		auto v = static_cast<int>((mark*1000.0) / progress_report_count);
		auto permille = v < 1000 ? v : 999;
		if (permille > thisp->progresswidget->value()) {
			QMetaObject::invokeMethod(thisp->progresswidget, "setValue", Qt::QueuedConnection,
																Q_ARG(int, permille));
			QApplication::processEvents();
		}

		// FIXME: Check if cancel was requested by e.g. Application quit
		if (thisp->progresswidget->wasCanceled()) throw ProgressCancelException();
	}
}

bool MainWindow::network_progress_func(const double permille)
{
	QMetaObject::invokeMethod(this->progresswidget, "setValue", Qt::QueuedConnection, Q_ARG(int, (int)permille));
	return (progresswidget && progresswidget->wasCanceled());
}

void MainWindow::updateRecentFiles(EditorInterface *edt)
{
	// Check that the canonical file path exists - only update recent files
	// if it does. Should prevent empty list items on initial open etc.
	QFileInfo fileinfo(edt->filepath);
	auto infoFileName = fileinfo.absoluteFilePath();
	QSettingsCached settings; // already set up properly via main.cpp
	auto files = settings.value("recentFileList").toStringList();
	files.removeAll(infoFileName);
	files.prepend(infoFileName);
	while (files.size() > UIUtils::maxRecentFiles) files.removeLast();
	settings.setValue("recentFileList", files);

	for (auto &widget : QApplication::topLevelWidgets()) {
		auto mainWin = qobject_cast<MainWindow *>(widget);
		if (mainWin) {
			mainWin->updateRecentFileActions();
		}
	}
}

void MainWindow::setTabToolBarVisible(int count)
{
	tabCount = count;
	tabToolBar->setVisible((tabCount > 1) && editorDock->isVisible());
}

void MainWindow::updatedAnimTval()
{
	bool t_ok;
	double t = this->e_tval->text().toDouble(&t_ok);
	// Clamp t to 0-1
	if (t_ok) {
		this->anim_tval = t < 0 ? 0.0 : ((t > 1.0) ? 1.0 : t);
	}
	else {
		this->anim_tval = 0.0;
	}
	emit actionRenderPreview(true);
}

void MainWindow::updatedAnimFps()
{
	bool fps_ok;
	double fps = this->e_fps->text().toDouble(&fps_ok);
	animate_timer->stop();
	if (fps_ok && fps > 0 && this->anim_numsteps > 0) {
		this->anim_step = int(this->anim_tval * this->anim_numsteps) % this->anim_numsteps;
		animate_timer->setSingleShot(false);
		animate_timer->setInterval(int(1000 / fps));
		animate_timer->start();
	}
}

void MainWindow::updatedAnimSteps()
{
	bool steps_ok;
	int numsteps = this->e_fsteps->text().toInt(&steps_ok);
	if (steps_ok) {
		this->anim_numsteps = numsteps;
		updatedAnimFps(); // Make sure we start
	}
	else {
		this->anim_numsteps = 0;
	}
	anim_dumping=false;
}

void MainWindow::updatedAnimDump(bool checked)
{
	if (!checked) this->anim_dumping = false;
}

// Only called from animate_timer
void MainWindow::updateTVal()
{
	if (this->anim_numsteps == 0) return;

	if (viewActionHideParameters->isVisible()) {
		if (this->parameterWidget->childHasFocus()) return;
	}
	
	if (this->anim_numsteps > 1) {
		this->anim_step = (this->anim_step + 1) % this->anim_numsteps;
		this->anim_tval = 1.0 * this->anim_step / this->anim_numsteps;
	}
	else if (this->anim_numsteps > 0) {
		this->anim_step = 0;
		this->anim_tval = 0.0;
	}
	const QString txt = QString::number(this->anim_tval, 'f', 5);
	this->e_tval->setText(txt);
}

/*!
	compiles the design. Calls compileDone() if anything was compiled
*/
void MainWindow::compile(bool reload, bool forcedone, bool rebuildParameterWidget)
{
	OpenSCAD::hardwarnings = Preferences::inst()->getValue("advanced/enableHardwarnings").toBool();
	OpenSCAD::parameterCheck = Preferences::inst()->getValue("advanced/enableParameterCheck").toBool();
	OpenSCAD::rangeCheck = Preferences::inst()->getValue("advanced/enableParameterRangeCheck").toBool();

	try{
		bool shouldcompiletoplevel = false;
		bool didcompile = false;

		compileErrors = 0;
		compileWarnings = 0;

		this->renderingTime.start();

		// Reload checks the timestamp of the toplevel file and refreshes if necessary,
		if (reload) {
			// Refresh files if it has changed on disk
			if (fileChangedOnDisk() && checkEditorModified()) {
				shouldcompiletoplevel = true;
				tabManager->refreshDocument();
				if (Preferences::inst()->getValue("advanced/autoReloadRaise").toBool()) {
					// reloading the 'same' document brings the 'old' one to front.
					this->raise();
				}
			}
			// If the file hasn't changed, we might still need to compile it
			// if we haven't yet compiled the current text.
			else {
				auto current_doc = activeEditor->toPlainText();
				if (current_doc.size() && last_compiled_doc.size() == 0) {
					shouldcompiletoplevel = true;
				}
			}
		}
		else {
			shouldcompiletoplevel = true;
		}

		if (this->parsed_module) {
			auto mtime = this->parsed_module->includesChanged();
			if (mtime > this->includes_mtime) {
				this->includes_mtime = mtime;
				shouldcompiletoplevel = true;
			}
		}
		// Parsing and dependency handling must run to completion even with stop on errors to prevent auto
		// reload picking up where it left off, thwarting the stop, so we turn off exceptions in PRINT.
		no_exceptions_for_warnings();
		if (shouldcompiletoplevel) {
			if (activeEditor->isContentModified()) saveBackup();
			parseTopLevelDocument(rebuildParameterWidget);
			didcompile = true;
		}

		if (didcompile && parser_error_pos != last_parser_error_pos) {
			if(last_parser_error_pos >= 0)
				emit unhighlightLastError();
			if (parser_error_pos >= 0)
				emit highlightError( parser_error_pos );
			last_parser_error_pos = parser_error_pos;
		}

		if (this->root_module) {
			auto mtime = this->root_module->handleDependencies();
			if (mtime > this->deps_mtime) {
				this->deps_mtime = mtime;
				PRINTB("Used file cache size: %d files", ModuleCache::instance()->size());
				didcompile = true;
			}
		}

		// Had any errors in the parse that would have caused exceptions via PRINT.
		if(would_have_thrown())
			throw HardWarningException("");
		// If we're auto-reloading, listen for a cascade of changes by starting a timer
		// if something changed _and_ there are any external dependencies
		if (reload && didcompile && this->root_module) {
			if (this->root_module->hasIncludes() ||	this->root_module->usesLibraries()) {
				this->waitAfterReloadTimer->start();
				this->procevents = false;
				return;
			}
		}

		compileDone(didcompile | forcedone);
	}catch(const HardWarningException&){
		exceptionCleanup();
	}
}

void MainWindow::waitAfterReload()
{
	no_exceptions_for_warnings();
	auto mtime = this->root_module->handleDependencies();
	auto stop = would_have_thrown();
	if (mtime > this->deps_mtime)
		this->deps_mtime = mtime;
	else
		if(!stop) {
			compile(true, true); // In case file itself or top-level includes changed during dependency updates
			return;
		}
	this->waitAfterReloadTimer->start();
}

void MainWindow::on_toolButtonCompileResultClose_clicked()
{
	frameCompileResult->hide();
}

void MainWindow::updateCompileResult()
{
	if ((compileErrors == 0) && (compileWarnings == 0)) {
		frameCompileResult->hide();
		return;
	}

	auto s = Settings::Settings::inst();
	if (!s->get(Settings::Settings::showWarningsIn3dView).toBool()) {
		return;
	}

	QString msg;
	if (compileErrors > 0) {
		if (activeEditor->filepath.isEmpty()) {
			msg = QString(_("Compile error."));
		} else {
			QFileInfo fileInfo(activeEditor->filepath);
			msg = QString(_("Error while compiling '%1'.")).arg(fileInfo.fileName());
		}
		toolButtonCompileResultIcon->setIcon(QIcon(QString::fromUtf8(":/icons/information-icons-error.png")));
	} else {
		const char *fmt = ngettext("Compilation generated %1 warning.", "Compilation generated %1 warnings.", compileWarnings);
		msg = QString(fmt).arg(compileWarnings);
		toolButtonCompileResultIcon->setIcon(QIcon(QString::fromUtf8(":/icons/information-icons-warning.png")));
	}
	QFontMetrics fm(labelCompileResultMessage->font());
	int sizeIcon = std::max(12, std::min(32, fm.height()));
	int sizeClose = std::max(10, std::min(32, fm.height()) - 4);
	toolButtonCompileResultIcon->setIconSize(QSize(sizeIcon, sizeIcon));
	toolButtonCompileResultClose->setIconSize(QSize(sizeClose, sizeClose));

	msg += _(" For details see <a href=\"#console\">console window</a>.");
	labelCompileResultMessage->setText(msg);
	frameCompileResult->show();
}

void MainWindow::compileDone(bool didchange)
{
	OpenSCAD::hardwarnings = Preferences::inst()->getValue("advanced/enableHardwarnings").toBool();
	try{
		const char *callslot;
		if (didchange) {
			updateTemporalVariables();
			instantiateRoot();
			updateCompileResult();
			callslot = afterCompileSlot;
		}
		else {
			callslot = "compileEnded";
		}

		this->procevents = false;
		QMetaObject::invokeMethod(this, callslot);
	}catch(const HardWarningException&){
		exceptionCleanup();
	}
}

void MainWindow::compileEnded()
{
	clearCurrentOutput();
	GuiLocker::unlock();
	if (designActionAutoReload->isChecked()) autoReloadTimer->start();
}

void MainWindow::instantiateRoot()
{
	// Go on and instantiate root_node, then call the continuation slot

  // Invalidate renderers before we kill the CSG tree
	this->qglview->setRenderer(nullptr);
#ifdef ENABLE_OPENCSG
	delete this->opencsgRenderer;
	this->opencsgRenderer = nullptr;
#endif
	delete this->thrownTogetherRenderer;
	this->thrownTogetherRenderer = nullptr;

	// Remove previous CSG tree
	delete this->absolute_root_node;
	this->absolute_root_node = nullptr;

	this->csgRoot.reset();
	this->normalizedRoot.reset();
	this->root_products.reset();

	this->root_node = nullptr;
	this->tree.setRoot(nullptr);

	boost::filesystem::path doc(activeEditor->filepath.toStdString());
	this->tree.setDocumentPath(doc.remove_filename().string());

	if (this->root_module) {
		// Evaluate CSG tree
		PRINT("Compiling design (CSG Tree generation)...");
		this->processEvents();

		AbstractNode::resetIndexCounter();

		// split these two lines - gcc 4.7 bug
		auto mi = ModuleInstantiation( "group" );
		this->root_inst = mi;

		FileContext filectx(&top_ctx);
		this->absolute_root_node = this->root_module->instantiateWithFileContext(&filectx, &this->root_inst, nullptr);
		this->updateCamera(filectx);
		
		if (this->absolute_root_node) {
			// Do we have an explicit root node (! modifier)?
			if (!(this->root_node = find_root_tag(this->absolute_root_node))) {
				this->root_node = this->absolute_root_node;
			}

			// FIXME: Consider giving away ownership of root_node to the Tree, or use reference counted pointers
			this->tree.setRoot(this->root_node);
		}
	}

	if (!this->root_node) {
		if (parser_error_pos < 0) {
			PRINT("ERROR: Compilation failed! (no top level object found)");
		} else {
			PRINT("ERROR: Compilation failed!");
		}
		PRINT(" ");
		this->processEvents();
	}
}

/*!
	Generates CSG tree for OpenCSG evaluation.
	Assumes that the design has been parsed and evaluated (this->root_node is set)
*/
void MainWindow::compileCSG()
{
	OpenSCAD::hardwarnings = Preferences::inst()->getValue("advanced/enableHardwarnings").toBool();
	try{
		assert(this->root_node);
		PRINT("Compiling design (CSG Products generation)...");
		this->processEvents();

		// Main CSG evaluation
		this->progresswidget = new ProgressWidget(this);
		connect(this->progresswidget, SIGNAL(requestShow()), this, SLOT(showProgress()));

#ifdef ENABLE_CGAL
			GeometryEvaluator geomevaluator(this->tree);
#else
			// FIXME: Will we support this?
#endif
#ifdef ENABLE_OPENCSG
			CSGTreeEvaluator csgrenderer(this->tree, &geomevaluator);
#endif

		progress_report_prep(this->root_node, report_func, this);
		try {
#ifdef ENABLE_OPENCSG
			this->processEvents();
			this->csgRoot = csgrenderer.buildCSGTree(*root_node);
#endif
			GeometryCache::instance()->print();
#ifdef ENABLE_CGAL
			CGALCache::instance()->print();
#endif
			this->processEvents();
		}
		catch (const ProgressCancelException &) {
			PRINT("CSG generation cancelled.");
		}catch(const HardWarningException &){
			PRINT("CSG generation cancelled due to hardwarning being enabled.");
		}
		progress_report_fin();
		updateStatusBar(nullptr);

		PRINT("Compiling design (CSG Products normalization)...");
		this->processEvents();

		size_t normalizelimit = 2 * Preferences::inst()->getValue("advanced/openCSGLimit").toUInt();
		CSGTreeNormalizer normalizer(normalizelimit);
	
		if (this->csgRoot) {
			this->normalizedRoot = normalizer.normalize(this->csgRoot);
			if (this->normalizedRoot) {
				this->root_products.reset(new CSGProducts());
				this->root_products->import(this->normalizedRoot);
			}
			else {
				this->root_products.reset();
				PRINT("WARNING: CSG normalization resulted in an empty tree");
				this->processEvents();
			}
		}

		const std::vector<shared_ptr<CSGNode> > &highlight_terms = csgrenderer.getHighlightNodes();
		if (highlight_terms.size() > 0) {
			PRINTB("Compiling highlights (%d CSG Trees)...", highlight_terms.size());
			this->processEvents();
		
			this->highlights_products.reset(new CSGProducts());
			for (unsigned int i = 0; i < highlight_terms.size(); i++) {
				auto nterm = normalizer.normalize(highlight_terms[i]);
				this->highlights_products->import(nterm);
			}
		}
		else {
			this->highlights_products.reset();
		}
	
		const auto &background_terms = csgrenderer.getBackgroundNodes();
		if (background_terms.size() > 0) {
			PRINTB("Compiling background (%d CSG Trees)...", background_terms.size());
			this->processEvents();
		
			this->background_products.reset(new CSGProducts());
			for (unsigned int i = 0; i < background_terms.size(); i++) {
				auto nterm = normalizer.normalize(background_terms[i]);
				this->background_products->import(nterm);
			}
		}
		else {
			this->background_products.reset();
		}

		if (this->root_products &&
				(this->root_products->size() >
				Preferences::inst()->getValue("advanced/openCSGLimit").toUInt())) {
			PRINTB("UI-WARNING: Normalized tree has %d elements!", this->root_products->size());
			PRINT("UI-WARNING: OpenCSG rendering has been disabled.");
		}
#ifdef ENABLE_OPENCSG
		else {
			PRINTB("Normalized CSG tree has %d elements",
						(this->root_products ? this->root_products->size() : 0));
			this->opencsgRenderer = new OpenCSGRenderer(this->root_products,
																								this->highlights_products,
																								this->background_products,
																								this->qglview->shaderinfo);
		}
#endif
		this->thrownTogetherRenderer = new ThrownTogetherRenderer(this->root_products,
																														this->highlights_products,
																														this->background_products);
		PRINT("Compile and preview finished.");
		int s = this->renderingTime.elapsed() / 1000;
		PRINTB("Total rendering time: %d hours, %d minutes, %d seconds\n", (s / (60*60)) % ((s / 60) % 60) % (s % 60));
		this->processEvents();
	}catch(const HardWarningException&){
		exceptionCleanup();
	}
}

void MainWindow::actionOpen()
{
	auto fileInfoList = UIUtils::openFiles(this);
	for(int i = 0; i < fileInfoList.size(); i++)
	{
		if (!fileInfoList[i].exists()) {
			return;
		}
		tabManager->open(fileInfoList[i].filePath());
	}
}

void MainWindow::actionNewWindow()
{
	new MainWindow(QStringList());
}

void MainWindow::actionOpenWindow()
{
	auto fileInfoList = UIUtils::openFiles(this);
	for(int i = 0; i < fileInfoList.size(); i++)
	{
		if (!fileInfoList[i].exists()) {
			return;
		}
		new MainWindow(QStringList(fileInfoList[i].filePath()));
	}
}

void MainWindow::actionOpenRecent()
{
	auto action = qobject_cast<QAction *>(sender());
	tabManager->open(action->data().toString());
}

void MainWindow::clearRecentFiles()
{
	QSettingsCached settings; // already set up properly via main.cpp
	QStringList files;
	settings.setValue("recentFileList", files);

	updateRecentFileActions();
}

void MainWindow::updateRecentFileActions()
{
	auto files = UIUtils::recentFiles();
	
	for (int i = 0; i < files.size(); ++i) {
		this->actionRecentFile[i]->setText(QFileInfo(files[i]).fileName().replace("&", "&&"));
		this->actionRecentFile[i]->setData(files[i]);
		this->actionRecentFile[i]->setVisible(true);
	}
	for (int i = files.size(); i < UIUtils::maxRecentFiles; ++i) {
		this->actionRecentFile[i]->setVisible(false);
	}
}

void MainWindow::show_examples()
{
	bool found_example = false;
	
	for (const auto &cat : UIUtils::exampleCategories()) {
		auto examples = UIUtils::exampleFiles(cat);
		auto menu = this->menuExamples->addMenu(gettext(cat.toStdString().c_str()));
		
		for (const auto &ex : examples) {
			auto openAct = new QAction(ex.fileName().replace("&", "&&"), this);
			connect(openAct, SIGNAL(triggered()), this, SLOT(actionOpenExample()));
			menu->addAction(openAct);
			openAct->setData(ex.canonicalFilePath());
			found_example = true;
		}
	}

	if (!found_example) {
		delete this->menuExamples;
		this->menuExamples = nullptr;
	}
}

void MainWindow::actionOpenExample()
{
	const auto action = qobject_cast<QAction *>(sender());
	if (action) {
		const auto &path = action->data().toString();
		tabManager->open(path);
	}
}

void MainWindow::writeBackup(QFile *file)
{
	// see MainWindow::saveBackup()
	file->resize(0);
	QTextStream writer(file);
	writer.setCodec("UTF-8");
	writer << activeEditor->toPlainText();
	this->parameterWidget->writeBackupFile(file->fileName());
	
	PRINTB("Saved backup file: %s", file->fileName().toUtf8().constData());
}

void MainWindow::saveBackup()
{
	auto path = PlatformUtils::backupPath();
	if ((!fs::exists(path)) && (!PlatformUtils::createBackupPath())) {
		PRINTB("UI-WARNING: Cannot create backup path: %s", path);
		return;
	}

	auto backupPath = QString::fromLocal8Bit(path.c_str());
	if (!backupPath.endsWith("/")) backupPath.append("/");

	QString basename = "unsaved";
	if (!activeEditor->filepath.isEmpty()) {
		auto fileInfo = QFileInfo(activeEditor->filepath);
		basename = fileInfo.baseName();
	}

	if (!this->tempFile) {
		this->tempFile = new QTemporaryFile(backupPath.append(basename + "-backup-XXXXXXXX.scad"));
	}

	if ((!this->tempFile->isOpen()) && (! this->tempFile->open())) {
		PRINT("UI-WARNING: Failed to create backup file");
		return;
	}
	return writeBackup(this->tempFile);
}

void MainWindow::actionSave()
{
	tabManager->save(activeEditor);
}

void MainWindow::actionSaveAs()
{
	tabManager->saveAs(activeEditor);
}

void MainWindow::actionShowLibraryFolder()
{
	auto path = PlatformUtils::userLibraryPath();
	if (!fs::exists(path)) {
		PRINTB("UI-WARNING: Library path %s doesn't exist. Creating", path);
		if (!PlatformUtils::createUserLibraryPath()) {
			PRINTB("UI-ERROR: Cannot create library path: %s",path);
		}
	}
	auto url = QString::fromStdString(path);
	//PRINTB("Opening file browser for %s", url.toStdString() );
	QDesktopServices::openUrl(QUrl::fromLocalFile(url));
}

void MainWindow::actionReload()
{
	if (checkEditorModified()) {
		fileChangedOnDisk(); // force cached autoReloadId to update
		tabManager->refreshDocument();
	}
}

void MainWindow::copyViewportTranslation()
{
	const auto vpt = qglview->cam.getVpt();
	const QString txt = QString("[ %1, %2, %3 ]")
		.arg(vpt.x(), 0, 'f', 2)
		.arg(vpt.y(), 0, 'f', 2)
		.arg(vpt.z(), 0, 'f', 2);
	QApplication::clipboard()->setText(txt);
}

void MainWindow::copyViewportRotation()
{
	const auto vpr = qglview->cam.getVpr();
	const QString txt = QString("[ %1, %2, %3 ]")
		.arg(vpr.x(), 0, 'f', 2)
		.arg(vpr.y(), 0, 'f', 2)
		.arg(vpr.z(), 0, 'f', 2);
	QApplication::clipboard()->setText(txt);
}

void MainWindow::copyViewportDistance()
{
	const QString txt = QString::number(qglview->cam.zoomValue(), 'f', 2);
	QApplication::clipboard()->setText(txt);
}

QList<double> MainWindow::getTranslation() const
{
	QList<double> ret;
	ret.append(qglview->cam.object_trans.x());
	ret.append(qglview->cam.object_trans.y());
	ret.append(qglview->cam.object_trans.z());
	return ret;
}

QList<double> MainWindow::getRotation() const
{
	QList<double> ret;
	ret.append(qglview->cam.object_rot.x());
	ret.append(qglview->cam.object_rot.y());
	ret.append(qglview->cam.object_rot.z());
	return ret;
}

void MainWindow::hideFind()
{
	find_panel->hide();
	activeEditor->findState = TabManager::FIND_HIDDEN;
	this->findInputField->setFindCount(activeEditor->updateFindIndicators(this->findInputField->text(), false));
	this->processEvents();
}

void MainWindow::showFind()
{
	this->findInputField->setFindCount(activeEditor->updateFindIndicators(this->findInputField->text()));
	this->processEvents();
	findTypeComboBox->setCurrentIndex(0);
	replaceInputField->hide();
	replaceButton->hide();
	replaceAllButton->hide();
	//replaceLabel->setVisible(false); 
	find_panel->show();
	activeEditor->findState = TabManager::FIND_VISIBLE;
	if (!activeEditor->selectedText().isEmpty()) {
		findInputField->setText(activeEditor->selectedText());
	}
	findInputField->setFocus();
	findInputField->selectAll();
}

void MainWindow::findString(QString textToFind)
{
	this->findInputField->setFindCount(activeEditor->updateFindIndicators(textToFind));
	this->processEvents();
	activeEditor->find(textToFind);
}

void MainWindow::showFindAndReplace()
{
	this->findInputField->setFindCount(activeEditor->updateFindIndicators(this->findInputField->text()));	
	this->processEvents();
	findTypeComboBox->setCurrentIndex(1); 
	replaceInputField->show();
	replaceButton->show();
	replaceAllButton->show();
	//replaceLabel->setVisible(true); 
	find_panel->show();
	activeEditor->findState = TabManager::FIND_REPLACE_VISIBLE;
	if (!activeEditor->selectedText().isEmpty()) {
		findInputField->setText(activeEditor->selectedText());
	}
	findInputField->setFocus();
	findInputField->selectAll();
}

void MainWindow::selectFindType(int type)
{
	if (type == 0) showFind();
	if (type == 1) showFindAndReplace();
}

void MainWindow::replace()
{
	activeEditor->replaceSelectedText(this->replaceInputField->text());
	activeEditor->find(this->findInputField->text());
}

void MainWindow::replaceAll()
{
	activeEditor->replaceAll(this->findInputField->text(), this->replaceInputField->text());
}

void MainWindow::convertTabsToSpaces()
{
	const auto text = activeEditor->toPlainText();
	
	QString converted;
  
	int cnt = 4;
	for (int idx = 0;idx < text.length();idx++) {
		auto c = text.at(idx);
		if (c == '\t') {
	    for (; cnt > 0; cnt--) {
				converted.append(' ');
	    }
		} else {
	    converted.append(c);
		}
		if (cnt <= 0 || c == '\n') {
	    cnt = 5;
		}
		cnt--;
	}
	activeEditor->setText(converted);
}

void MainWindow::findNext()
{
	activeEditor->find(this->findInputField->text(), true);
}

void MainWindow::findPrev()
{
	activeEditor->find(this->findInputField->text(), true, true);
}

void MainWindow::useSelectionForFind()
{
	findInputField->setText(activeEditor->selectedText());
}

void MainWindow::updateFindBuffer(QString s)
{
	QApplication::clipboard()->setText(s, QClipboard::FindBuffer);
}

void MainWindow::findBufferChanged()
{
	auto t = QApplication::clipboard()->text(QClipboard::FindBuffer);
	// The convention seems to be to not update the search field if the findbuffer is empty
	if (!t.isEmpty()) {
		findInputField->setText(t);
	}
}

bool MainWindow::event(QEvent* event) {
	if (event->type() == InputEvent::eventType) {
		InputEvent *inputEvent = dynamic_cast<InputEvent *>(event);
		if (inputEvent) {
			inputEvent->deliver(this);
		}
		event->accept();
		return true;
	}
	return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent *event)
{
	if (obj == find_panel) {
		if (event->type() == QEvent::KeyPress) {
			auto keyEvent = static_cast<QKeyEvent*>(event);
			if (keyEvent->key() == Qt::Key_Escape) {
				this->hideFind();
				return true;
			}
		}
		return false;
	}
	return QMainWindow::eventFilter(obj, event);
}

void MainWindow::updateTemporalVariables()
{
	this->top_ctx.set_variable("$t", ValuePtr(this->anim_tval));

	auto camVpt = qglview->cam.getVpt();
	Value::VectorType vpt;
	vpt.push_back(ValuePtr(camVpt.x()));
	vpt.push_back(ValuePtr(camVpt.y()));
	vpt.push_back(ValuePtr(camVpt.z()));
	this->top_ctx.set_variable("$vpt", ValuePtr(vpt));

	auto camVpr = qglview->cam.getVpr();
	Value::VectorType vpr;
	vpr.push_back(ValuePtr(camVpr.x()));
	vpr.push_back(ValuePtr(camVpr.y()));
	vpr.push_back(ValuePtr(camVpr.z()));
	top_ctx.set_variable("$vpr", ValuePtr(vpr));

	top_ctx.set_variable("$vpd", ValuePtr(qglview->cam.zoomValue()));
}


/*!
 * Update the viewport camera by evaluating the special variables. If they
 * are assigned on top-level, the values are used to change the camera
 * rotation, translation and distance.
 */
void MainWindow::updateCamera(const FileContext &ctx)
{
	double x, y, z;
	const auto vpr = ctx.lookup_variable("$vpr");
	if (vpr->getVec3(x, y, z, 0.0)){
		qglview->cam.setVpr(x, y, z);
	}else{
		PRINTB("UI-WARNING: Unable to convert $vpr=%s to a vec3 or vec2 of numbers", vpr->toEchoString());
	}

	const auto vpt = ctx.lookup_variable("$vpt");
	if (vpt->getVec3(x, y, z, 0.0)){
		qglview->cam.setVpt(x, y, z);
	}else{
		PRINTB("UI-WARNING: Unable to convert $vpt=%s to a vec3 or vec2 of numbers", vpt->toEchoString());
	}

	const auto vpd = ctx.lookup_variable("$vpd");
	if (vpd->type() == Value::ValueType::NUMBER){
		qglview->cam.setVpd(vpd->toDouble());
	}else{
		PRINTB("UI-WARNING: Unable to convert $vpd=%s to a number", vpd->toEchoString());
	}
}

/*!
	Returns true if the current document is a file on disk and that file has new content.
	Returns false if a file on disk has disappeared or if we haven't yet saved.
*/
bool MainWindow::fileChangedOnDisk()
{
	if (!activeEditor->filepath.isEmpty()) {
		struct stat st;
		memset(&st, 0, sizeof(struct stat));
		bool valid = (stat(activeEditor->filepath.toLocal8Bit(), &st) == 0);
		// If file isn't there, just return and use current editor text
		if (!valid) return false;

		auto newid = str(boost::format("%x.%x") % st.st_mtime % st.st_size);

		if (newid != activeEditor->autoReloadId) {
			activeEditor->autoReloadId = newid;
			return true;
		}
	}
	return false;
}

/*!
	Returns true if anything was compiled.
*/
void MainWindow::parseTopLevelDocument(bool rebuildParameterWidget)
{
	customizerEditor = nullptr;
	this->parameterWidget->setEnabled(false);
	resetSuppressedMessages();

	this->last_compiled_doc = activeEditor->toPlainText();

	auto fulltext =
		std::string(this->last_compiled_doc.toUtf8().constData()) +
		"\n\x03\n" + commandline_commands;

	auto fnameba = activeEditor->filepath.toLocal8Bit();
	const char* fname = activeEditor->filepath.isEmpty() ? "" : fnameba;
	delete this->parsed_module;
	this->root_module = parse(this->parsed_module, fulltext, fname, fname, false) ? this->parsed_module : nullptr;

	if (this->root_module!=nullptr) {
		//add parameters as annotation in AST
		CommentParser::collectParameters(fulltext,this->root_module);
		this->parameterWidget->setParameters(this->root_module,rebuildParameterWidget);
		this->parameterWidget->applyParameters(this->root_module);
		customizerEditor = activeEditor;
		this->parameterWidget->setEnabled(true);
	}
}

void MainWindow::changeParameterWidget()
{
	viewActionHideParameters->setVisible(true);
}

void MainWindow::checkAutoReload()
{
	if (!activeEditor->filepath.isEmpty()) {
		actionReloadRenderPreview();
	}
}

void MainWindow::autoReloadSet(bool on)
{
	QSettingsCached settings;
	settings.setValue("design/autoReload",designActionAutoReload->isChecked());
	if (on) {
		autoReloadTimer->start(200);
	} else {
		autoReloadTimer->stop();
	}
}

bool MainWindow::checkEditorModified()
{
	if (activeEditor->isContentModified()) {
		auto ret = QMessageBox::warning(this, _("Application"),
				_("The document has been modified.\n"
				"Do you really want to reload the file?"),
				QMessageBox::Yes | QMessageBox::No);
		if (ret != QMessageBox::Yes) {
			designActionAutoReload->setChecked(false);
			return false;
		}
	}
	return true;
}

void MainWindow::actionReloadRenderPreview()
{
	if (GuiLocker::isLocked()) return;
	GuiLocker::lock();
	autoReloadTimer->stop();
	setCurrentOutput();

	// PRINT("Parsing design (AST generation)...");
	// this->processEvents();
	this->afterCompileSlot = "csgReloadRender";
	this->procevents = true;
	this->top_ctx.set_variable("$preview", ValuePtr(true));
	compile(true);
}

void MainWindow::csgReloadRender()
{
	if (this->root_node) compileCSG();

	// Go to non-CGAL view mode
	if (viewActionThrownTogether->isChecked()) {
		viewModeThrownTogether();
	}
	else {
#ifdef ENABLE_OPENCSG
		viewModePreview();
#else
		viewModeThrownTogether();
#endif
	}
	compileEnded();
}

void MainWindow::actionRenderPreview(bool rebuildParameterWidget)
{
	static bool preview_requested;

	preview_requested=true;
	if (GuiLocker::isLocked()) return;
	GuiLocker::lock();
	autoReloadTimer->stop();
	preview_requested=false;
	setCurrentOutput();

	PRINT("Parsing design (AST generation)...");
	this->processEvents();
	this->afterCompileSlot = "csgRender";
	this->procevents = !viewActionAnimate->isChecked();
	this->top_ctx.set_variable("$preview", ValuePtr(true));
	compile(false,false,rebuildParameterWidget);
	if (preview_requested) {
		// if the action was called when the gui was locked, we must request it one more time
		// however, it's not possible to call it directly NOR make the loop
		// it must be called from the mainloop
		QTimer::singleShot(0, this, SLOT(actionRenderPreview()));
	}
}

void MainWindow::csgRender()
{
	if (this->root_node) compileCSG();

	// Go to non-CGAL view mode
	if (viewActionThrownTogether->isChecked()) {
		viewModeThrownTogether();
	}
	else {
#ifdef ENABLE_OPENCSG
		viewModePreview();
#else
		viewModeThrownTogether();
#endif
	}

	if (e_dump->isChecked() && animate_timer->isActive()) {
		if (anim_dumping && anim_dump_start_step == anim_step) {
			anim_dumping=false;
			e_dump->setChecked(false);
		} else {
			if (!anim_dumping) {
				anim_dumping = true;
				anim_dump_start_step = anim_step;
			}
			// Force reading from front buffer. Some configurations will read from the back buffer here.
			glReadBuffer(GL_FRONT);
			QImage img = this->qglview->grabFrameBuffer();
			QString filename = QString("frame%1.png").arg(this->anim_step, 5, 10, QChar('0'));
			img.save(filename, "PNG");
		}
	}

	compileEnded();
}

void MainWindow::action3DPrint()
{
#ifdef ENABLE_3D_PRINTING
	if (GuiLocker::isLocked()) return;
	GuiLocker lock;

	setCurrentOutput();

	//Make sure we can export:
	const unsigned int dim = 3;
	if (!canExport(dim)) return;

	const auto printService = PrintService::inst();
	auto printInitDialog = new PrintInitDialog();
	auto printInitResult = printInitDialog->exec();
	printInitDialog->deleteLater();
	if (printInitResult == QDialog::Rejected) {
		return;
	}

	const auto selectedService = printInitDialog->getResult();
	Preferences::Preferences::inst()->updateGUI();

	switch (selectedService) {
	case print_service_t::PRINT_SERVICE:
		PRINTB("Sending design to print service %s...", printService->getDisplayName().toStdString());
		sendToPrintService();
		break;
	case print_service_t::OCTOPRINT:
		PRINT("Sending design to OctoPrint...");
		sendToOctoPrint();
		break;
	default:
		break;
	}
#endif
}

void MainWindow::sendToOctoPrint()
{
#ifdef ENABLE_3D_PRINTING
	OctoPrint octoPrint;

	if (octoPrint.url().trimmed().isEmpty()) {
		PRINT("ERROR: OctoPrint connection not configured. Please check preferences.");
		return;
	}

	Settings::Settings *s = Settings::Settings::inst();
	const QString fileFormat = QString::fromStdString(s->get(Settings::Settings::octoPrintFileFormat).toString());
	FileFormat exportFileFormat{FileFormat::STL};
	if (fileFormat == "OFF") {
		exportFileFormat = FileFormat::OFF;
	} else if (fileFormat == "AMF") {
		exportFileFormat = FileFormat::AMF;
	} else if (fileFormat == "3MF") {
		exportFileFormat = FileFormat::_3MF;
	} else {
		exportFileFormat = FileFormat::STL;
	}

	QTemporaryFile exportFile{QDir::temp().filePath("OpenSCAD.XXXXXX." + fileFormat.toLower())};
	if (!exportFile.open()) {
		PRINT("Could not open temporary file.");
		return;
	}
	const QString exportFileName = exportFile.fileName();
	exportFile.close();

	QString userFileName;
	if (activeEditor->filepath.isEmpty()) {
		userFileName = exportFileName;
	} else {
		QFileInfo fileInfo{activeEditor->filepath};
		userFileName = fileInfo.baseName() + "." + fileFormat.toLower();
	}

	exportFileByName(this->root_geom, exportFileFormat, exportFileName.toLocal8Bit().constData(), exportFileName.toUtf8());

	try {
		this->progresswidget = new ProgressWidget(this);
		connect(this->progresswidget, SIGNAL(requestShow()), this, SLOT(showProgress()));
		const QString fileUrl = octoPrint.upload(exportFileName, userFileName, [this](double v) -> bool { return network_progress_func(v); });

		const std::string action = s->get(Settings::Settings::octoPrintAction).toString();
		if (action == "upload") {
			return;
		}

		const QString slicer = QString::fromStdString(s->get(Settings::Settings::octoPrintSlicerEngine).toString());
		const QString profile = QString::fromStdString(s->get(Settings::Settings::octoPrintSlicerProfile).toString());
		octoPrint.slice(fileUrl, slicer, profile, action != "slice", action == "print");
	} catch (const NetworkException& e) {
		PRINTB("ERROR: %s", e.getErrorMessage().toStdString());
	}

	updateStatusBar(nullptr);
#endif
}

void MainWindow::sendToPrintService()
{
#ifdef ENABLE_3D_PRINTING
	//Keeps track of how many times we've exported and tries to create slightly unique filenames.
	//Not mission critical, since non-unique file names are fine for the API, just harder to 
	//differentiate between in customer support later.
	static unsigned int printCounter = 0;
	
	QTemporaryFile exportFile;
	if (!exportFile.open()) {
		PRINT("ERROR: Could not open temporary file.");
		return;
	}
	const QString exportFilename = exportFile.fileName();
	
	//Render the stl to a temporary file:
	exportFileByName(this->root_geom, FileFormat::STL, exportFilename.toLocal8Bit().constData(), exportFilename.toUtf8());

	//Create a name that the order process will use to refer to the file. Base it off of the project name
	QString userFacingName = "unsaved.stl";
	if (!activeEditor->filepath.isEmpty()) {
		const QString baseName = QFileInfo(activeEditor->filepath).baseName();
		userFacingName = QString{"%1_%2.stl"}.arg(baseName).arg(printCounter++);
	}

	QFile file(exportFilename);
	if (!file.open(QIODevice::ReadOnly)) {
		PRINT("ERROR: Unable to open exported STL file.");
		return;
	}
	const QString fileContentBase64 = file.readAll().toBase64();

	if (fileContentBase64.length() > PrintService::inst()->getFileSizeLimit()) {
		const auto msg = QString{_("Exported design exceeds the service upload limit of (%1 MB).")}.arg(PrintService::inst()->getFileSizeLimitMB());
		QMessageBox::warning(this, _("Upload Error"), msg, QMessageBox::Ok);
		PRINTB("ERROR: %s", msg.toStdString());
		return;
	}
	
	//Upload the file to the 3D Printing server and get the corresponding url to see it.
	//The result is put in partUrl.
	try
	{
		this->progresswidget = new ProgressWidget(this);
		connect(this->progresswidget, SIGNAL(requestShow()), this, SLOT(showProgress()));
        const QString partUrl = PrintService::inst()->upload(userFacingName, fileContentBase64, [this](double v) -> bool { return network_progress_func(v); });
		QDesktopServices::openUrl(QUrl{partUrl});
	} catch (const NetworkException& e) {
		PRINTB("ERROR: %s", e.getErrorMessage().toStdString());
    }

	updateStatusBar(nullptr);
#endif
}

#ifdef ENABLE_CGAL

void MainWindow::actionRender()
{
	if (GuiLocker::isLocked()) return;
	GuiLocker::lock();
	autoReloadTimer->stop();
	setCurrentOutput();

	PRINT("Parsing design (AST generation)...");
	this->processEvents();
	this->afterCompileSlot = "cgalRender";
	this->procevents = true;
	this->top_ctx.set_variable("$preview", ValuePtr(false));
	compile(false);
}

void MainWindow::cgalRender()
{
	if (!this->root_module || !this->root_node) {
		compileEnded();
		return;
	}

	this->qglview->setRenderer(nullptr);
	delete this->cgalRenderer;
	this->cgalRenderer = nullptr;
	this->root_geom.reset();

	PRINT("Rendering Polygon Mesh using CGAL...");

	this->progresswidget = new ProgressWidget(this);
	connect(this->progresswidget, SIGNAL(requestShow()), this, SLOT(showProgress()));

	progress_report_prep(this->root_node, report_func, this);

	this->cgalworker->start(this->tree);
}

void MainWindow::actionRenderDone(shared_ptr<const Geometry> root_geom)
{
	progress_report_fin();

	unsigned int s = this->renderingTime.elapsed() / 1000;

	if (root_geom) {
		GeometryCache::instance()->print();
#ifdef ENABLE_CGAL
		CGALCache::instance()->print();
#endif

		PRINTB("Total rendering time: %d hours, %d minutes, %d seconds", (s / (60*60)) % ((s / 60) % 60) % (s % 60));

		if (root_geom && !root_geom->isEmpty()) {
			if (const CGAL_Nef_polyhedron *N = dynamic_cast<const CGAL_Nef_polyhedron *>(root_geom.get())) {
				if (N->getDimension() == 3) {
					bool simple = N->p3->is_simple();
					PRINT("   Top level object is a 3D object:");
					PRINTB("   Simple:     %6s", (simple ? "yes" : "no"));
					PRINTB("   Vertices:   %6d", N->p3->number_of_vertices());
					PRINTB("   Halfedges:  %6d", N->p3->number_of_halfedges());
					PRINTB("   Edges:      %6d", N->p3->number_of_edges());
					PRINTB("   Halffacets: %6d", N->p3->number_of_halffacets());
					PRINTB("   Facets:     %6d", N->p3->number_of_facets());
					PRINTB("   Volumes:    %6d", N->p3->number_of_volumes());
					if (!simple) {
						PRINT("UI-WARNING: Object may not be a valid 2-manifold and may need repair!");
					}
				}
			}
			else if (const PolySet *ps = dynamic_cast<const PolySet *>(root_geom.get())) {
				assert(ps->getDimension() == 3);
				PRINT("   Top level object is a 3D object:");
				PRINTB("   Facets:     %6d", ps->numPolygons());
			} else if (const Polygon2d *poly = dynamic_cast<const Polygon2d *>(root_geom.get())) {
				PRINT("   Top level object is a 2D object:");
				PRINTB("   Contours:     %6d", poly->outlines().size());
			} else {
				assert(false && "Unknown geometry type");
			}
		}
		PRINT("Rendering finished.\n");

		this->root_geom = root_geom;
		this->cgalRenderer = new CGALRenderer(root_geom);
		// Go to CGAL view mode
		if (viewActionWireframe->isChecked()) viewModeWireframe();
		else viewModeSurface();
	}
	else {
		PRINT("UI-WARNING: No top level geometry to render");
		PRINT(" ");
	}

	updateStatusBar(nullptr);

	if (Preferences::inst()->getValue("advanced/enableSoundNotification").toBool() && 
		Preferences::inst()->getValue("advanced/timeThresholdOnRenderCompleteSound").toUInt() <= s)
	{
		QSound::play(":sounds/complete.wav");
	}

	renderedEditor = activeEditor;
	activeEditor->contentsRendered = true;
	compileEnded();
}

#endif /* ENABLE_CGAL */

/**
 * Switch version label and progress widget. When switching to the progress
 * widget, the new instance is passed by the caller.
 * In case of resetting back to the version label, nullptr will be passed and
 * multiple calls can happen. So this method must guard against adding the
 * version label multiple times.
 *
 * @param progressWidget a pointer to the progress widget to show or nullptr in
 * case the display should switch back to the version label.
 */
void MainWindow::updateStatusBar(ProgressWidget *progressWidget)
{
	auto sb = this->statusBar();
	if (progressWidget == nullptr) {
		if (this->progresswidget != nullptr) {
			sb->removeWidget(this->progresswidget);
			delete this->progresswidget;
			this->progresswidget = nullptr;
		}
		if (versionLabel == nullptr) {
			versionLabel = new QLabel("OpenSCAD " + QString::fromStdString(openscad_displayversionnumber));
			sb->addPermanentWidget(this->versionLabel);
		}
	} else {
		if (this->versionLabel != nullptr) {
			sb->removeWidget(this->versionLabel);
			delete this->versionLabel;
			this->versionLabel = nullptr;
		}
		sb->addPermanentWidget(progressWidget);
	}
}

void MainWindow::exceptionCleanup(){
	PRINT("Execution aborted");
	PRINT(" ");
	GuiLocker::unlock();
	if (designActionAutoReload->isChecked()) autoReloadTimer->start();
}

void MainWindow::actionDisplayAST()
{
	setCurrentOutput();
	auto e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(tabStopWidth);
	e->setWindowTitle("AST Dump");
	e->setReadOnly(true);
	if (root_module) {
		e->setPlainText(QString::fromUtf8(root_module->dump("").c_str()));
	} else {
		e->setPlainText("No AST to dump. Please try compiling first...");
	}
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();
}

void MainWindow::actionDisplayCSGTree()
{
	setCurrentOutput();
	auto e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(tabStopWidth);
	e->setWindowTitle("CSG Tree Dump");
	e->setReadOnly(true);
	if (this->root_node) {
		e->setPlainText(QString::fromUtf8(this->tree.getString(*this->root_node, "  ").c_str()));
	} else {
		e->setPlainText("No CSG to dump. Please try compiling first...");
	}
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();
}

void MainWindow::actionDisplayCSGProducts()
{
	setCurrentOutput();
	auto e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(tabStopWidth);
	e->setWindowTitle("CSG Products Dump");
	e->setReadOnly(true);
	e->setPlainText(QString("\nCSG before normalization:\n%1\n\n\nCSG after normalization:\n%2\n\n\nCSG rendering chain:\n%3\n\n\nHighlights CSG rendering chain:\n%4\n\n\nBackground CSG rendering chain:\n%5\n")
									
	.arg(this->csgRoot ? QString::fromUtf8(this->csgRoot->dump().c_str()) : "N/A",
	this->normalizedRoot ? QString::fromUtf8(this->normalizedRoot->dump().c_str()) : "N/A",
	this->root_products ? QString::fromUtf8(this->root_products->dump().c_str()) : "N/A",
	this->highlights_products ? QString::fromUtf8(this->highlights_products->dump().c_str()) : "N/A",
	this->background_products ? QString::fromUtf8(this->background_products->dump().c_str()) : "N/A"));
	
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();
}

void MainWindow::actionCheckValidity()
{
	if (GuiLocker::isLocked()) return;
	GuiLocker lock;
#ifdef ENABLE_CGAL
	setCurrentOutput();

	if (!this->root_geom) {
		PRINT("Nothing to validate! Try building first (press F6).");
		clearCurrentOutput();
		return;
	}

	if (this->root_geom->getDimension() != 3) {
		PRINT("Current top level object is not a 3D object.");
		clearCurrentOutput();
		return;
	}

	bool valid = false;
	shared_ptr<const CGAL_Nef_polyhedron> N;
	if (auto ps = dynamic_cast<const PolySet *>(this->root_geom.get())) {
		N.reset(CGALUtils::createNefPolyhedronFromGeometry(*ps));
	}
	if (N || (N = dynamic_pointer_cast<const CGAL_Nef_polyhedron>(this->root_geom))) {
		valid = N->p3 ? N->p3->is_valid() : false;
	}
	PRINTB("   Valid:      %6s", (valid ? "yes" : "no"));
	clearCurrentOutput();
#endif /* ENABLE_CGAL */
}

//Returns if we can export (true) or not(false) (bool)
//Separated into it's own function for re-use.
bool MainWindow::canExport(unsigned int dim)
{
	if (!this->root_geom) {
		PRINT("ERROR: Nothing to export! Try rendering first (press F6).");
		clearCurrentOutput();
		return false;
	}

	// editor has changed since last render
	if (!activeEditor->contentsRendered) {
		auto ret = QMessageBox::warning(this, "Application",
				"The current tab has been modified since its last render (F6).\n"
				"Do you really want to export the previous content?",
				QMessageBox::Yes | QMessageBox::No);
		if (ret != QMessageBox::Yes) {
			return false;
		}
	}

	// other tab contents most recently rendered
	if(renderedEditor != activeEditor) {
		auto ret = QMessageBox::warning(this, "Application",
				"The rendered data is of different tab.\n"
				"Do you really want to export the another tab's content?",
				QMessageBox::Yes | QMessageBox::No);
		if (ret != QMessageBox::Yes) {
			return false;
		}
	}

	if (this->root_geom->getDimension() != dim) {
		PRINTB("UI-ERROR: Current top level object is not a %dD object.", dim);
		clearCurrentOutput();
		return false;
	}

	if (this->root_geom->isEmpty()) {
		PRINT("UI-ERROR: Current top level object is empty.");
		clearCurrentOutput();
		return false;
	}

	auto N = dynamic_cast<const CGAL_Nef_polyhedron *>(this->root_geom.get());
	if (N && !N->p3->is_simple()) {
		PRINT("UI-WARNING: Object may not be a valid 2-manifold and may need repair! See https://en.wikibooks.org/wiki/OpenSCAD_User_Manual/STL_Import_and_Export");
	}
	
	return true;
}

#ifdef ENABLE_CGAL
void MainWindow::actionExport(FileFormat format, const char *type_name, const char *suffix, unsigned int dim)
#else
	void MainWindow::actionExport(FileFormat, QString, QString, unsigned int, QString)
#endif
{
    //Setting filename skips the file selection dialog and uses the path provided instead.
    
	if (GuiLocker::isLocked()) return;
	GuiLocker lock;
#ifdef ENABLE_CGAL
	setCurrentOutput();
	
	//Return if something is wrong and we can't export.
	if (! canExport(dim))
		return;
	auto title = QString(_("Export %1 File")).arg(type_name);
	auto filter = QString(_("%1 Files (*%2)")).arg(type_name, suffix);
	auto exportFilename = QFileDialog::getSaveFileName(this, title, exportPath(suffix), filter);
	if (exportFilename.isEmpty()) {
		clearCurrentOutput();
		return;
	}
	this->export_paths[suffix] = exportFilename;
	exportFileByName(this->root_geom, format,
		exportFilename.toLocal8Bit().constData(),
		exportFilename.toUtf8());
	fileExportedMessage(type_name, exportFilename);
	clearCurrentOutput();
#endif /* ENABLE_CGAL */
}

void MainWindow::actionExportSTL()
{
	actionExport(FileFormat::STL, "STL", ".stl", 3);
}

void MainWindow::actionExport3MF()
{
	actionExport(FileFormat::_3MF, "3MF", ".3mf", 3);
}

void MainWindow::actionExportOFF()
{
	actionExport(FileFormat::OFF, "OFF", ".off", 3);
}

void MainWindow::actionExportAMF()
{
	actionExport(FileFormat::AMF, "AMF", ".amf", 3);
}

void MainWindow::actionExportDXF()
{
	actionExport(FileFormat::DXF, "DXF", ".dxf", 2);
}

void MainWindow::actionExportSVG()
{
	actionExport(FileFormat::SVG, "SVG", ".svg", 2);
}

void MainWindow::actionExportCSG()
{
	setCurrentOutput();

	if (!this->root_node) {
		PRINT("ERROR: Nothing to export. Please try compiling first.");
		clearCurrentOutput();
		return;
	}
	const auto suffix = ".csg";
	auto csg_filename = QFileDialog::getSaveFileName(this,
		_("Export CSG File"), exportPath(suffix), _("CSG Files (*.csg)"));

	if (csg_filename.isEmpty()) {
		clearCurrentOutput();
		return;
	}

	std::ofstream fstream(csg_filename.toLocal8Bit());
	if (!fstream.is_open()) {
		PRINTB("Can't open file \"%s\" for export", csg_filename.toLocal8Bit().constData());
	}
	else {
		fstream << this->tree.getString(*this->root_node, "\t") << "\n";
		fstream.close();
		fileExportedMessage("CSG", csg_filename);
		this->export_paths[suffix] = csg_filename;
	}

	clearCurrentOutput();
}

void MainWindow::actionExportImage()
{
	// Grab first to make sure dialog box isn't part of the grabbed image
	qglview->grabFrame();
	const auto suffix = ".png";
	auto img_filename = QFileDialog::getSaveFileName(this,
			_("Export Image"),  exportPath(suffix), _("PNG Files (*.png)"));
	if (!img_filename.isEmpty()) {
		qglview->save(img_filename.toLocal8Bit().constData());
		this->export_paths[suffix] = img_filename;
		setCurrentOutput();
		fileExportedMessage("PNG", img_filename);
		clearCurrentOutput();
	}
}

void MainWindow::actionCopyViewport()
{
	const auto &image = qglview->grabFrame();
	auto clipboard = QApplication::clipboard();
	clipboard->setImage(image);
}

void MainWindow::actionFlushCaches()
{
	GeometryCache::instance()->clear();
#ifdef ENABLE_CGAL
	CGALCache::instance()->clear();
#endif
	dxf_dim_cache.clear();
	dxf_cross_cache.clear();
	ModuleCache::instance()->clear();
}

void MainWindow::viewModeActionsUncheck()
{
	viewActionPreview->setChecked(false);
#ifdef ENABLE_CGAL
	viewActionSurfaces->setChecked(false);
	viewActionWireframe->setChecked(false);
#endif
	viewActionThrownTogether->setChecked(false);
}

#ifdef ENABLE_OPENCSG

/*!
	Go to the OpenCSG view mode.
	Falls back to thrown together mode if OpenCSG is not available
*/
void MainWindow::viewModePreview()
{
	if (this->qglview->hasOpenCSGSupport()) {
		viewModeActionsUncheck();
		viewActionPreview->setChecked(true);
		this->qglview->setRenderer(this->opencsgRenderer ? (Renderer *)this->opencsgRenderer : (Renderer *)this->thrownTogetherRenderer);
		this->qglview->updateColorScheme();
		this->qglview->updateGL();
	} else {
		viewModeThrownTogether();
	}
}

#endif /* ENABLE_OPENCSG */

#ifdef ENABLE_CGAL

void MainWindow::viewModeSurface()
{
	viewModeActionsUncheck();
	viewActionSurfaces->setChecked(true);
	this->qglview->setShowFaces(true);
	this->qglview->setRenderer(this->cgalRenderer);
	this->qglview->updateColorScheme();
	this->qglview->updateGL();
}

void MainWindow::viewModeWireframe()
{
	viewModeActionsUncheck();
	viewActionWireframe->setChecked(true);
	this->qglview->setShowFaces(false);
	this->qglview->setRenderer(this->cgalRenderer);
	this->qglview->updateColorScheme();
	this->qglview->updateGL();
}

#endif /* ENABLE_CGAL */

void MainWindow::viewModeThrownTogether()
{
	viewModeActionsUncheck();
	viewActionThrownTogether->setChecked(true);
	this->qglview->setRenderer(this->thrownTogetherRenderer);
	this->qglview->updateColorScheme();
	this->qglview->updateGL();
}

void MainWindow::viewModeShowEdges()
{
	QSettingsCached settings;
	settings.setValue("view/showEdges",viewActionShowEdges->isChecked());
	this->qglview->setShowEdges(viewActionShowEdges->isChecked());
	this->qglview->updateGL();
}

void MainWindow::viewModeShowAxes()
{
	bool showaxes = viewActionShowAxes->isChecked();
	QSettingsCached settings;
	settings.setValue("view/showAxes", showaxes);
	this->viewActionShowScaleProportional->setEnabled(showaxes);
	this->qglview->setShowAxes(showaxes);
	this->qglview->updateGL();
}

void MainWindow::viewModeShowCrosshairs()
{
	QSettingsCached settings;
	settings.setValue("view/showCrosshairs",viewActionShowCrosshairs->isChecked());
	this->qglview->setShowCrosshairs(viewActionShowCrosshairs->isChecked());
	this->qglview->updateGL();
}

void MainWindow::viewModeShowScaleProportional()
{
	QSettingsCached settings;
	settings.setValue("view/showScaleProportional",viewActionShowScaleProportional->isChecked());
	this->qglview->setShowScaleProportional(viewActionShowScaleProportional->isChecked());
	this->qglview->updateGL();
}

void MainWindow::viewModeAnimate()
{
	if (viewActionAnimate->isChecked()) {
		animate_panel->show();
		actionRenderPreview();
		updatedAnimFps();
	} else {
		animate_panel->hide();
		animate_timer->stop();
	}
}

bool MainWindow::isEmpty()
{
	return activeEditor->toPlainText().isEmpty();
}

void MainWindow::animateUpdateDocChanged()
{
	auto current_doc = activeEditor->toPlainText(); 
	if (current_doc != last_compiled_doc) {
		animateUpdate();
	}
}

void MainWindow::animateUpdate()
{
	if (animate_panel->isVisible()) {
		bool fps_ok;
		double fps = this->e_fps->text().toDouble(&fps_ok);
		if (fps_ok && fps <= 0 && !animate_timer->isActive()) {
			animate_timer->stop();
			animate_timer->setSingleShot(true);
			animate_timer->setInterval(50);
			animate_timer->start();
		}
	}
}

void MainWindow::viewAngleTop()
{
	qglview->cam.object_rot << 90,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewAngleBottom()
{
	qglview->cam.object_rot << 270,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewAngleLeft()
{
	qglview->cam.object_rot << 0,0,90;
	this->qglview->updateGL();
}

void MainWindow::viewAngleRight()
{
	qglview->cam.object_rot << 0,0,270;
	this->qglview->updateGL();
}

void MainWindow::viewAngleFront()
{
	qglview->cam.object_rot << 0,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewAngleBack()
{
	qglview->cam.object_rot << 0,0,180;
	this->qglview->updateGL();
}

void MainWindow::viewAngleDiagonal()
{
	qglview->cam.object_rot << 35,0,-25;
	this->qglview->updateGL();
}

void MainWindow::viewCenter()
{
	qglview->cam.object_trans << 0,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewPerspective()
{
	QSettingsCached settings;
	settings.setValue("view/orthogonalProjection",false);
	viewActionPerspective->setChecked(true);
	viewActionOrthogonal->setChecked(false);
	this->qglview->setOrthoMode(false);
	this->qglview->updateGL();
}

void MainWindow::viewOrthogonal()
{
	QSettingsCached settings;
	settings.setValue("view/orthogonalProjection",true);
	viewActionPerspective->setChecked(false);
	viewActionOrthogonal->setChecked(true);
	this->qglview->setOrthoMode(true);
	this->qglview->updateGL();
}

void MainWindow::viewTogglePerspective()
{
	QSettingsCached settings;
	if (settings.value("view/orthogonalProjection").toBool()) {
		viewPerspective();
	} else {
		viewOrthogonal();
	}
}
void MainWindow::viewResetView()
{
	this->qglview->resetView();
	this->qglview->updateGL();
}

void MainWindow::viewAll()
{
	this->qglview->viewAll();
	this->qglview->updateGL();
}

void MainWindow::on_editorDock_visibilityChanged(bool)
{
	changedTopLevelEditor(editorDock->isFloating());
	tabToolBar->setVisible((tabCount > 1) && editorDock->isVisible());
}

void MainWindow::on_consoleDock_visibilityChanged(bool)
{
	changedTopLevelConsole(consoleDock->isFloating());
}

void MainWindow::on_parameterDock_visibilityChanged(bool)
{
    parameterTopLevelChanged(parameterDock->isFloating());
}

void MainWindow::changedTopLevelEditor(bool topLevel)
{
	setDockWidgetTitle(editorDock, QString(_("Editor")), topLevel);
}

void MainWindow::editorTopLevelChanged(bool topLevel)
{
	setDockWidgetTitle(editorDock, QString(_("Editor")), topLevel);
	if(topLevel)
	{
		this->removeToolBar(tabToolBar);
		((QVBoxLayout *)editorDockContents->layout())->insertWidget(0, tabToolBar);
	}
	else
	{
		editorDockContents->layout()->removeWidget(tabToolBar);
		this->addToolBar(tabToolBar);
	}
	tabToolBar->setVisible((tabCount > 1) && editorDock->isVisible());
}

void MainWindow::changedTopLevelConsole(bool topLevel)
{
	setDockWidgetTitle(consoleDock, QString(_("Console")), topLevel);
}

void MainWindow::consoleTopLevelChanged(bool topLevel)
{
	setDockWidgetTitle(consoleDock, QString(_("Console")), topLevel);

    Qt::WindowFlags flags = (consoleDock->windowFlags() & ~Qt::WindowType_Mask) | Qt::Window;
	if(topLevel)
	{
		consoleDock->setWindowFlags(flags);
		consoleDock->show();
	}
}

void MainWindow::parameterTopLevelChanged(bool topLevel)
{
    setDockWidgetTitle(parameterDock, QString(_("Customizer")), topLevel);
}

void MainWindow::setDockWidgetTitle(QDockWidget *dockWidget, QString prefix, bool topLevel)
{
	QString title(prefix);
	if (topLevel) {
		const QFileInfo fileInfo(activeEditor->filepath);
		QString fname = _("Untitled.scad");
		if(!fileInfo.fileName().isEmpty())
			fname = fileInfo.fileName();
		title += " (" + fname.replace("&", "&&") + ")";
	}
	dockWidget->setWindowTitle(title);
}

void MainWindow::hideToolbars()
{
	QSettingsCached settings;
	bool shouldHide = viewActionHideToolBars->isChecked();
	settings.setValue("view/hideToolbar", shouldHide);

	if (shouldHide) {
		viewerToolBar->hide();
		editortoolbar->hide();
	} else {
		viewerToolBar->show();
		editortoolbar->show();
	}
}

void MainWindow::hideEditor()
{
	if (viewActionHideEditor->isChecked()) {
		editorDock->close();
	} else {
		editorDock->show();
	}
}

void MainWindow::showConsole()
{
	viewActionHideConsole->setChecked(false);
	consoleDock->show();
	frameCompileResult->hide();
}

void MainWindow::hideConsole()
{
	if (viewActionHideConsole->isChecked()) {
		consoleDock->hide();
	} else {
		consoleDock->show();
	}
}

void MainWindow::hideParameters()
{
	if (viewActionHideParameters->isChecked()) {
		parameterDock->hide();
	} else {
		parameterDock->show();
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls()) {
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent *event)
{
	setCurrentOutput();
	const QList<QUrl> urls = event->mimeData()->urls();
	for (int i = 0; i < urls.size(); i++) {
		if (urls[i].scheme() != "file") continue;
		handleFileDrop(urls[i].toLocalFile());
	}
	clearCurrentOutput();
}

void MainWindow::handleFileDrop(const QString &filename)
{
	const QFileInfo fileInfo(filename);
	const auto suffix = fileInfo.suffix().toLower();
	const auto cmd = knownFileExtensions[suffix];
	if (cmd.isEmpty()) {
		tabManager->open(filename);
	} else {
		activeEditor->insert(cmd.arg(filename));
	}
}

void MainWindow::helpAbout()
{
	qApp->setWindowIcon(QApplication::windowIcon());
	auto dialog = new AboutDialog(this);
	dialog->exec();
}

void MainWindow::helpHomepage()
{
	UIUtils::openHomepageURL();
}

void MainWindow::helpManual()
{
	UIUtils::openUserManualURL();
}

void MainWindow::helpCheatSheet()
{
	UIUtils::openCheatSheetURL();
}

void MainWindow::helpLibrary()
{
	if (!this->library_info_dialog) {
		QString rendererInfo(qglview->getRendererInfo().c_str());
		auto dialog = new LibraryInfoDialog(rendererInfo);
		this->library_info_dialog = dialog;
	}
	this->library_info_dialog->show();
}

void MainWindow::helpFontInfo()
{
	if (!this->font_list_dialog) {
		auto dialog = new FontListDialog();
		this->font_list_dialog = dialog;
	}
	this->font_list_dialog->update_font_list();
	this->font_list_dialog->show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (tabManager->shouldClose()) {
		QSettingsCached settings;
		settings.setValue("window/size", size());
		settings.setValue("window/position", pos());
		settings.setValue("window/state", saveState());
		if (this->tempFile) {
			delete this->tempFile;
			this->tempFile = nullptr;
		}
		this->editorDock->disableSettingsUpdate();
		this->consoleDock->disableSettingsUpdate();
		this->parameterDock->disableSettingsUpdate();

		event->accept();
	} else {
		event->ignore();
	}
}

void MainWindow::preferences()
{
	Preferences::inst()->show();
	Preferences::inst()->activateWindow();
	Preferences::inst()->raise();
}

void MainWindow::setColorScheme(const QString &scheme)
{
	RenderSettings::inst()->colorscheme = scheme.toStdString();
	this->qglview->setColorScheme(scheme.toStdString());
	this->qglview->updateGL();
}

void MainWindow::setFont(const QString &family, uint size)
{
	QFont font;
	if (!family.isEmpty()) font.setFamily(family);
	else font.setFixedPitch(true);
	if (size > 0)	font.setPointSize(size);
	font.setStyleHint(QFont::TypeWriter);
	activeEditor->setFont(font);
}

void MainWindow::quit()
{
	QCloseEvent ev;
	QApplication::sendEvent(QApplication::instance(), &ev);
	if (ev.isAccepted()) QApplication::instance()->quit();
  // FIXME: Cancel any CGAL calculations
#ifdef Q_OS_MAC
	CocoaUtils::endApplication();
#endif
}

void MainWindow::consoleOutput(const std::string &msg, void *userdata)
{
	// Invoke the method in the main thread in case the output
	// originates in a worker thread.
	auto thisp = static_cast<MainWindow*>(userdata);
	QMetaObject::invokeMethod(thisp, "consoleOutput", Q_ARG(QString, QString::fromStdString(msg)));
}

void MainWindow::consoleOutput(const QString &msg)
{
	auto c = this->console->textCursor();
	c.movePosition(QTextCursor::End);
	this->console->setTextCursor(c);

	if (msg.startsWith("WARNING:") || msg.startsWith("DEPRECATED:")) {
		this->compileWarnings++;
		this->console->appendHtml("<span style=\"color: black; background-color: #ffffb0;\">" + QT_HTML_ESCAPE(QString(msg)) + "</span>");
	} else if (msg.startsWith("UI-WARNING:") || msg.startsWith("FONT-WARNING:") || msg.startsWith("EXPORT-WARNING:")) {
		this->console->appendHtml("<span style=\"color: black; background-color: #ffffb0;\">" + QT_HTML_ESCAPE(QString(msg)) + "</span>");
	} else if (msg.startsWith("ERROR:")) {
		this->compileErrors++;
		this->console->appendHtml("<span style=\"color: black; background-color: #ffb0b0;\">" + QT_HTML_ESCAPE(QString(msg)) + "</span>");
	} else if (msg.startsWith("EXPORT-ERROR:") || msg.startsWith("UI-ERROR:") || msg.startsWith("PARSER-ERROR:")) {
		this->console->appendHtml("<span style=\"color: black; background-color: #ffb0b0;\">" + QT_HTML_ESCAPE(QString(msg)) + "</span>");
	} else if (msg.startsWith("TRACE:")) {
		this->console->appendHtml("<span style=\"color: black; background-color: #d0d0ff;\">" + QT_HTML_ESCAPE(QString(msg)) + "</span>");
	} else {
		QString qmsg = msg;
		if(qmsg.contains('\t') && !qmsg.contains("<pre>", Qt::CaseInsensitive))
			this->console->appendPlainText(qmsg);
		else {
			qmsg.replace("\n","<br>");
			this->console->appendHtml(qmsg);
		}
	}
	this->processEvents();
}

void MainWindow::setCurrentOutput()
{
	set_output_handler(&MainWindow::consoleOutput, this);
}

void MainWindow::hideCurrentOutput()
{
	set_output_handler(&MainWindow::noOutput, this);
}

void MainWindow::clearCurrentOutput()
{
	set_output_handler(nullptr, nullptr);
}

void MainWindow::openCSGSettingsChanged()
{
#ifdef ENABLE_OPENCSG
	OpenCSG::setOption(OpenCSG::AlgorithmSetting, Preferences::inst()->getValue("advanced/forceGoldfeather").toBool() ?
	OpenCSG::Goldfeather : OpenCSG::Automatic);
#endif
}

void MainWindow::processEvents()
{
	if (this->procevents) QApplication::processEvents();
}

QString MainWindow::exportPath(const char *suffix) {
	QString path;
	auto path_it = this->export_paths.find(suffix);
	if(path_it != export_paths.end())
		path = path_it->second;
	else
		if(activeEditor->filepath.isEmpty())
			path = QString(PlatformUtils::userDocumentsPath().c_str()) + QString(_("/Untitled")) + suffix;
		else {
			auto info = QFileInfo(activeEditor->filepath);
			path = info.absolutePath() + QString(_("/")) + info.completeBaseName() + suffix;
		}
	return path;
}
