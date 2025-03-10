#pragma once

#include <QMainWindow>
#include <QSettings>

#include "qtgettext.h"
#include "ui_Preferences.h"
#include "settings.h"

template <class WidgetPtr>
class BlockSignals
{
public:
	BlockSignals(WidgetPtr w) : w(w) { w->blockSignals(true); }
	~BlockSignals() { w->blockSignals(false); }
	WidgetPtr operator->() const { return w; }

	BlockSignals(const BlockSignals&) = delete;
	BlockSignals& operator=(BlockSignals const&) = delete;
private:
	WidgetPtr w;
};

class Preferences : public QMainWindow, public Ui::Preferences
{
	Q_OBJECT;

public:
	~Preferences();
	
	static void create(QStringList colorSchemes);
	static Preferences *inst();
	
	QVariant getValue(const QString &key) const;
	void init();
	void apply_win() const;
	void updateGUI();
	void fireEditorConfigChanged() const;

public slots:
	void actionTriggered(class QAction *);
	void featuresCheckBoxToggled(bool);
	void on_stackedWidget_currentChanged(int);
	void on_colorSchemeChooser_itemSelectionChanged();
	void on_fontChooser_activated(const QString &);
	void on_fontSize_currentIndexChanged(const QString &);
	void on_syntaxHighlight_activated(const QString &);
	void on_openCSGWarningBox_toggled(bool);
	void on_enableOpenCSGBox_toggled(bool);
	void on_cgalCacheSizeMBEdit_textChanged(const QString &);
	void on_polysetCacheSizeMBEdit_textChanged(const QString &);
	void on_opencsgLimitEdit_textChanged(const QString &);
	void on_forceGoldfeatherBox_toggled(bool);
	void on_mouseWheelZoomBox_toggled(bool);
	void on_localizationCheckBox_toggled(bool);
	void on_autoReloadRaiseCheckBox_toggled(bool);
	void on_updateCheckBox_toggled(bool);
	void on_snapshotCheckBox_toggled(bool);
	void on_reorderCheckBox_toggled(bool);
	void on_undockCheckBox_toggled(bool);
	void on_checkNowButton_clicked();
	void on_launcherBox_toggled(bool);
	void on_enableSoundOnRenderCompleteCheckBox_toggled(bool);
	void on_enableHardwarningsCheckBox_toggled(bool);
	void on_enableParameterCheckBox_toggled(bool);
	void on_enableRangeCheckBox_toggled(bool);
	void on_enableHidapiTraceCheckBox_toggled(bool);
	void on_checkBoxShowWarningsIn3dView_toggled(bool);
	void on_checkBoxMouseCentricZoom_toggled(bool);
	void on_timeThresholdOnRenderCompleteSoundEdit_textChanged(const QString &);
  //
	// editor settings
  //

	// Indentation
	void on_checkBoxAutoIndent_toggled(bool);
	void on_checkBoxBackspaceUnindents_toggled(bool);
	void on_comboBoxIndentUsing_activated(int);
	void on_spinBoxIndentationWidth_valueChanged(int);
	void on_spinBoxTabWidth_valueChanged(int);
	void on_comboBoxTabKeyFunction_activated(int);
	void on_comboBoxShowWhitespace_activated(int);
	void on_spinBoxShowWhitespaceSize_valueChanged(int);
	
	// Line wrap
	void on_comboBoxLineWrap_activated(int);
	void on_comboBoxLineWrapIndentationStyle_activated(int);
	void on_spinBoxLineWrapIndentationIndent_valueChanged(int);
	void on_comboBoxLineWrapVisualizationStart_activated(int);
	void on_comboBoxLineWrapVisualizationEnd_activated(int);

	// Display
	void on_checkBoxHighlightCurrentLine_toggled(bool);
	void on_checkBoxEnableBraceMatching_toggled(bool);
	void on_checkBoxEnableLineNumbers_toggled(bool);

	// Print
	void on_pushButtonOctoPrintCheckConnection_clicked();
	void on_pushButtonOctoPrintSlicingEngine_clicked();
	void on_comboBoxOctoPrintSlicingEngine_activated(int);
	void on_pushButtonOctoPrintSlicingProfile_clicked();
	void on_comboBoxOctoPrintSlicingProfile_activated(int);
	void on_comboBoxOctoPrintAction_activated(int);
	void on_comboBoxOctoPrintFileFormat_activated(int);
	void on_lineEditOctoPrintURL_editingFinished();
	void on_lineEditOctoPrintApiKey_editingFinished();
	void on_pushButtonOctoPrintApiKey_clicked();

signals:
	void requestRedraw() const;
	void updateUndockMode(bool undockMode) const;
	void updateReorderMode(bool undockMode) const;
	void fontChanged(const QString &family, uint size) const;
	void colorSchemeChanged(const QString &scheme) const;
	void openCSGSettingsChanged() const;
	void syntaxHighlightChanged(const QString &s) const;
	void editorConfigChanged() const;
	void ExperimentalChanged() const ;
	void updateMouseCentricZoom(bool state) const;

private:
    Preferences(QWidget *parent = nullptr);
	void keyPressEvent(QKeyEvent *e) override;
	void showEvent(QShowEvent *e) override;
	void closeEvent(QCloseEvent *e) override;
	void removeDefaultSettings();
	void setupFeaturesPage();
	void writeSettings();
	void hidePasswords();
	void addPrefPage(QActionGroup *group, QAction *action, QWidget *widget);

	/** Initialize checkbox from the settings value */
	void initCheckBox(const BlockSignals<QCheckBox *>& checkBox, const Settings::SettingsEntry& entry);
	/** Initialize combobox list values from the settings range values */
	void initComboBox(const BlockSignals<QComboBox *>& comboBox, const Settings::SettingsEntry& entry);
	/** Initialize spinbox min/max values from the settings range values */
	void initSpinBoxRange(const BlockSignals<QSpinBox *>& spinBox, const Settings::SettingsEntry& entry);
	/** Initialize spinbox double value from the settings value */
	void initSpinBoxDouble(const BlockSignals<QSpinBox *>& spinBox, const Settings::SettingsEntry& entry);
	/** Update combobox from current settings */
	void updateComboBox(const BlockSignals<QComboBox *>& comboBox, const Settings::SettingsEntry& entry);
	/** Set value from combobox to settings */
	void applyComboBox(QComboBox * comboBox, int val, Settings::SettingsEntry& entry);

	QSettings::SettingsMap defaultmap;
	QHash<const QAction *, QWidget *> prefPages;

	static Preferences *instance;
	static const char *featurePropertyName;
};
