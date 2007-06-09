/**
 * This file is a part of Qtpfsgui package.
 * ---------------------------------------------------------------------- 
 * Copyright (C) 2006,2007 Giuseppe Rota
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * ---------------------------------------------------------------------- 
 *
 * @author Giuseppe Rota <grota@users.sourceforge.net>
 */

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include "maingui_impl.h"
#include "fileformat/pfstiff.h"
#include "tonemappingdialog_impl.h"
#include "../generated_uic/ui_help_about.h"
#include "config.h"
#include "transplant_impl.h"
// #include "align_impl.h"

pfs::Frame* readEXRfile  (const char * filename);
pfs::Frame* readRGBEfile (const char * filename);
pfs::Frame* rotateFrame( pfs::Frame* inputpfsframe, bool clock_wise );
void writeRGBEfile (pfs::Frame* inputpfsframe, const char* outfilename);
void writeEXRfile  (pfs::Frame* inputpfsframe, const char* outfilename);

MainGui::MainGui(QWidget *p) : QMainWindow(p), currenthdr(NULL), settings("Qtpfsgui", "Qtpfsgui") {
	setupUi(this);
	connect(this->fileExitAction, SIGNAL(triggered()), this, SLOT(fileExit()));
	workspace = new QWorkspace(this);
	workspace->setScrollBarsEnabled( TRUE );
	setCentralWidget(workspace);

	RecentDirHDRSetting=settings.value(KEY_RECENT_PATH_LOAD_SAVE_HDR,QDir::currentPath()).toString();
	qtpfsgui_options=new qtpfsgui_opts();
	load_options(qtpfsgui_options);

	setWindowTitle("Qtpfsgui v"QTPFSGUIVERSION);
// 	qDebug("Qtpfsgui v"QTPFSGUIVERSION);
	connect(workspace, SIGNAL(windowActivated(QWidget*)), this, SLOT(updateActions(QWidget *)) );
	connect(fileNewAction, SIGNAL(triggered()), this, SLOT(fileNewViaWizard()));
	connect(fileOpenAction, SIGNAL(triggered()), this, SLOT(fileOpen()));
	connect(fileSaveAsAction, SIGNAL(triggered()), this, SLOT(fileSaveAs()));
	connect(TonemapAction, SIGNAL(triggered()), this, SLOT(tonemap_requested()));
	connect(rotateccw, SIGNAL(triggered()), this, SLOT(rotateccw_requested()));
	connect(rotatecw, SIGNAL(triggered()), this, SLOT(rotatecw_requested()));
	connect(actionResizeHDR, SIGNAL(triggered()), this, SLOT(resize_requested()));
	connect(Low_dynamic_range,SIGNAL(triggered()),this,SLOT(current_mdiwindow_ldr_exposure()));
	connect(Fit_to_dynamic_range,SIGNAL(triggered()),this,SLOT(current_mdiwindow_fit_exposure()));
	connect(Shrink_dynamic_range,SIGNAL(triggered()),this,SLOT(current_mdiwindow_shrink_exposure()));
	connect(Extend_dynamic_range,SIGNAL(triggered()),this,SLOT(current_mdiwindow_extend_exposure()));
	connect(Decrease_exposure,SIGNAL(triggered()),this,SLOT(current_mdiwindow_decrease_exposure()));
	connect(Increase_exposure,SIGNAL(triggered()),this,SLOT(current_mdiwindow_increase_exposure()));
	connect(zoomInAct,SIGNAL(triggered()),this,SLOT(current_mdiwindow_zoomin()));
	connect(zoomOutAct,SIGNAL(triggered()),this,SLOT(current_mdiwindow_zoomout()));
	connect(fitToWindowAct,SIGNAL(toggled(bool)),this,SLOT(current_mdiwindow_fit_to_win(bool)));
	connect(normalSizeAct,SIGNAL(triggered()),this,SLOT(current_mdiwindow_original_size()));
	connect(helpAction,SIGNAL(triggered()),this,SLOT(helpAbout()));
	connect(actionAbout_Qt,SIGNAL(triggered()),qApp,SLOT(aboutQt()));
	connect(OptionsAction,SIGNAL(triggered()),this,SLOT(options_called()));
	connect(Transplant_Exif_Data_action,SIGNAL(triggered()),this,SLOT(transplant_called()));
	connect(actionTile,SIGNAL(triggered()),workspace,SLOT(tile()));
	connect(actionCascade,SIGNAL(triggered()),workspace,SLOT(cascade()));
// 	connect(actionAlign_Images,SIGNAL(triggered()),this,SLOT(align_called()));

        for (int i = 0; i < MaxRecentFiles; ++i) {
            recentFileActs[i] = new QAction(this);
            recentFileActs[i]->setVisible(false);
            connect(recentFileActs[i], SIGNAL(triggered()),
                    this, SLOT(openRecentFile()));
        }
        separatorRecentFiles = menuFile->addSeparator();
	for (int i = 0; i < MaxRecentFiles; ++i)
		menuFile->addAction(recentFileActs[i]);
	updateRecentFileActions();

	this->showMaximized();
	statusBar()->showMessage(tr("Ready.... Now open an Hdr or create one!"),17000);
}

void MainGui::fileNewViaWizard() {
	wizard=new HdrWizardForm (this,&(qtpfsgui_options->dcraw_options));
	if (wizard->exec() == QDialog::Accepted) {
		HdrViewer *newmdi=new HdrViewer( this, qtpfsgui_options->negcolor, qtpfsgui_options->naninfcolor, true); //true means needs saving
		newmdi->updateHDR(wizard->getPfsFrameHDR());
		workspace->addWindow(newmdi);
		newmdi->setWindowTitle(wizard->getCaptionTEXT());
		newmdi->show();
	}
	delete wizard;
}

void MainGui::fileOpen() {
	QString filetypes = tr("All Hdr formats ");
	filetypes += "(*.hdr *.pic *.tiff *.tif *.pfs *.exr *.crw *.cr2 *.nef *.dng *.mrw *.orf *.kdc *.dcr *.arw *.raf *.ptx *.pef *.x3f *.raw" ;
#ifndef _WIN32
	filetypes += " *.exr);;OpenEXR (*.exr" ;
#endif
	filetypes += ");;Radiance RGBE (*.hdr *.pic);;";
	filetypes += "TIFF Images (*.tiff *.tif);;";
	filetypes += "RAW Images (*.crw *.cr2 *.nef *.dng *.mrw *.orf *.kdc *.dcr *.arw *.raf *.ptx *.pef *.x3f *.raw);;";
	filetypes += "PFS Stream (*.pfs)";
	QString opened = QFileDialog::getOpenFileName(
			this,
			tr("Load an Hdr file..."),
			RecentDirHDRSetting,
			filetypes );
	if (loadFile(opened))
		setCurrentFile(opened);
}

bool MainGui::loadFile(QString opened) {
if( ! opened.isEmpty() ) {
	QFileInfo qfi(opened);
	// if the new dir, the one just chosen by the user, is different from the one stored in the settings, update the settings.
	if (RecentDirHDRSetting != qfi.path() ) {
		// update internal field variable
		RecentDirHDRSetting=qfi.path();
		settings.setValue(KEY_RECENT_PATH_LOAD_SAVE_HDR, RecentDirHDRSetting);
	}
	if (!qfi.isReadable()) {
	    QMessageBox::critical(this,tr("Aborting..."),tr("File is not readable (check existence, permissions,...)"), QMessageBox::Ok,QMessageBox::NoButton);
	    return false;
	}
	HdrViewer *newhdr;
	pfs::Frame* hdrpfsframe = NULL;
	QString extension=qfi.suffix().toUpper();
	bool rawinput=(extension!="PFS")&&(extension!="EXR")&&(extension!="HDR")&&(!extension.startsWith("TIF"));
#ifndef _WIN32
	if (extension=="EXR") {
		hdrpfsframe = readEXRfile(qfi.filePath().toUtf8().constData());
	} else
#endif
	       if (extension=="HDR") {
		hdrpfsframe = readRGBEfile(qfi.filePath().toUtf8().constData());
	} else if (extension=="PFS") {
		pfs::DOMIO pfsio;
		hdrpfsframe=pfsio.readFrame(qfi.filePath());
		hdrpfsframe->convertXYZChannelsToRGB();
	} else if (extension.startsWith("TIF")) {
		TiffReader reader(qfi.filePath().toUtf8().constData());
		hdrpfsframe = reader.readIntoPfsFrame(); //from 8,16,32,logluv to pfs::Frame
	} 
	else if (rawinput) {
		hdrpfsframe = readRAWfile(qfi.filePath().toUtf8().constData(), &(qtpfsgui_options->dcraw_options));
	}
	else {
		QMessageBox::warning(this,tr("Aborting..."),tr("Qtpfsgui supports only <br>Radiance rgbe (hdr), PFS, raw, hdr tiff and OpenEXR (linux only) <br>files up until now."),
				 QMessageBox::Ok,QMessageBox::NoButton);
		return false;
	}
	assert(hdrpfsframe!=NULL);
	newhdr=new HdrViewer(this,qtpfsgui_options->negcolor,qtpfsgui_options->naninfcolor, false);
	newhdr->updateHDR(hdrpfsframe);
	newhdr->filename=opened;
	newhdr->setWindowTitle(opened);
	workspace->addWindow(newhdr);
	newhdr->show();
	return true;
}
return false;
}

void MainGui::fileSaveAs()
{
	assert(currenthdr!=NULL);
	QStringList filetypes;
#ifndef _WIN32
	filetypes += tr("All Hdr formats (*.exr *.hdr *.pic *.tiff *.tif *.pfs)");
	filetypes += "OpenEXR (*.exr)";
#else
	filetypes += tr("All Hdr formats (*.hdr *.pic *.tiff *.tif *.pfs)");
#endif
	filetypes += "Radiance RGBE (*.hdr *.pic)";
	filetypes += "HDR TIFF (*.tiff *.tif)";
	filetypes += "PFS Stream (*.pfs)";

	QFileDialog *fd = new QFileDialog(this);
	fd->setWindowTitle(tr("Save the HDR..."));
	fd->setDirectory(RecentDirHDRSetting);
// 	fd->selectFile(...);
	fd->setFileMode(QFileDialog::AnyFile);
	fd->setFilters(filetypes);
	fd->setAcceptMode(QFileDialog::AcceptSave);
	fd->setConfirmOverwrite(true);
#ifdef _WIN32
	fd->setDefaultSuffix("hdr");
#else
	fd->setDefaultSuffix("exr");
#endif
	if (fd->exec()) {
		QString fname=(fd->selectedFiles()).at(0);
		if(!fname.isEmpty()) {
			QFileInfo qfi(fname);
			// if the new dir, the one just chosen by the user, is different from the one stored in the settings, update the settings.
			if (RecentDirHDRSetting != qfi.path() ) {
				// update internal field variable
				RecentDirHDRSetting=qfi.path();
				settings.setValue(KEY_RECENT_PATH_LOAD_SAVE_HDR, RecentDirHDRSetting);
			}
#ifndef _WIN32
			if (qfi.suffix().toUpper()=="EXR") {
				writeEXRfile  (currenthdr->getHDRPfsFrame(),qfi.filePath().toUtf8().constData());
			} else 
#endif
				if (qfi.suffix().toUpper()=="HDR") {
				writeRGBEfile (currenthdr->getHDRPfsFrame(), qfi.filePath().toUtf8().constData());
			} else if (qfi.suffix().toUpper().startsWith("TIF")) {
				TiffWriter tiffwriter(qfi.filePath().toUtf8().constData(), currenthdr->getHDRPfsFrame());
				if (qtpfsgui_options->saveLogLuvTiff)
					tiffwriter.writeLogLuvTiff();
				else
					tiffwriter.writeFloatTiff();
			} else if (qfi.suffix().toUpper()=="PFS") {
				pfs::DOMIO pfsio;
				(currenthdr->getHDRPfsFrame())->convertRGBChannelsToXYZ();
				pfsio.writeFrame(currenthdr->getHDRPfsFrame(),qfi.filePath());
				(currenthdr->getHDRPfsFrame())->convertXYZChannelsToRGB();
			} else {
				QMessageBox::warning(this,tr("Aborting..."), tr("Qtpfsgui supports only <br>Radiance rgbe (hdr), PFS, hdr tiff and OpenEXR (linux only) <br>files up until now."),
				QMessageBox::Ok,QMessageBox::NoButton);
				delete fd;
				return;
			}
			setCurrentFile(fname);
			currenthdr->NeedsSaving=false;
			currenthdr->filename=fname;
			currenthdr->setWindowTitle(fname);
		}
	}
	delete fd;
}

void MainGui::updateActions( QWidget * w )
{
	TonemapAction->setEnabled(w!=NULL);
	fileSaveAsAction->setEnabled(w!=NULL);
	rotateccw->setEnabled(w!=NULL);
	rotatecw->setEnabled(w!=NULL);
	menuHDR_Histogram->setEnabled(w!=NULL);
	Low_dynamic_range->setEnabled(w!=NULL);
	Fit_to_dynamic_range->setEnabled(w!=NULL);
	Shrink_dynamic_range->setEnabled(w!=NULL);
	Extend_dynamic_range->setEnabled(w!=NULL);
	Decrease_exposure->setEnabled(w!=NULL);
	Increase_exposure->setEnabled(w!=NULL);
	actionResizeHDR->setEnabled(w!=NULL);
	if (w!=NULL) {
		currenthdr=(HdrViewer*)(workspace->activeWindow());
		if (currenthdr->getFittingWin()) {
			normalSizeAct->setEnabled(false);
			zoomInAct->setEnabled(false);
			zoomOutAct->setEnabled(false);
			fitToWindowAct->setEnabled(true);
		} else {
			zoomOutAct->setEnabled(currenthdr->getScaleFactor() > 0.222);
			zoomInAct->setEnabled(currenthdr->getScaleFactor() < 3.0);
			fitToWindowAct->setEnabled(true);
			normalSizeAct->setEnabled(true);
		}
	} else {
		currenthdr=NULL;
		normalSizeAct->setEnabled(false);
		zoomInAct->setEnabled(false);
		zoomOutAct->setEnabled(false);
		fitToWindowAct->setEnabled(false);
	}
}

void MainGui::tonemap_requested() {
	assert(currenthdr!=NULL);
	if (currenthdr->NeedsSaving) {
		QMessageBox::warning(this,tr("Save the HDR..."),tr("Save the Hdr before tone mapping."),
		QMessageBox::Ok,QMessageBox::NoButton);
		fileSaveAs();
		if (currenthdr->NeedsSaving)
			return;
	}
	QFileInfo test(qtpfsgui_options->tempfilespath);
	if (test.isWritable() && test.exists() && test.isDir()) {
		this->setDisabled(true);
		TonemappingWindow *tmodialog=new TonemappingWindow(this,currenthdr->getHDRPfsFrame(),qtpfsgui_options->tempfilespath, currenthdr->filename);
		connect(tmodialog,SIGNAL(closing()),this,SLOT(reEnableHdrViewer()));
		tmodialog->show();
		tmodialog->setAttribute(Qt::WA_DeleteOnClose);
	} else {
		QMessageBox::critical(this,tr("Error..."),tr("Qtpfsgui needs to cache its results using temporary files, but the currently selected directory is not valid.<br>Please choose a valid path in Tools -> Configure Qtpfsgui... -> Tone mapping."),
		QMessageBox::Ok,QMessageBox::NoButton);
	}
}

void MainGui::reEnableHdrViewer() {
	this->setEnabled(true);
}

void MainGui::rotateccw_requested() {
	dispatchrotate(false);
}

void MainGui::rotatecw_requested() {
	dispatchrotate(true);
}

void MainGui::dispatchrotate( bool clockwise) {
	assert(currenthdr!=NULL);
	rotateccw->setEnabled(false);
	rotatecw->setEnabled(false);
	QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );
	pfs::Frame *rotated=rotateFrame(currenthdr->getHDRPfsFrame(),clockwise);
	//updateHDR() method takes care of deleting its previous pfs::Frame* buffer.
	currenthdr->updateHDR(rotated);
	if (! currenthdr->NeedsSaving) {
		currenthdr->NeedsSaving=true;
		currenthdr->setWindowTitle(currenthdr->windowTitle().prepend("(*) "));
	}
	QApplication::restoreOverrideCursor();
	rotateccw->setEnabled(true);
	rotatecw->setEnabled(true);
}

void MainGui::resize_requested() {
	assert(currenthdr!=NULL);
	ResizeDialog *resizedialog=new ResizeDialog(this,currenthdr->getHDRPfsFrame());
	if (resizedialog->exec() == QDialog::Accepted) {
		QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );
		//updateHDR() method takes care of deleting its previous pfs::Frame* buffer.
		currenthdr->updateHDR(resizedialog->getResizedFrame());
		if (! currenthdr->NeedsSaving) {
			currenthdr->NeedsSaving=true;
			currenthdr->setWindowTitle(currenthdr->windowTitle().prepend("(*) "));
		}
		QApplication::restoreOverrideCursor();
	}
	delete resizedialog;
}

void MainGui::current_mdiwindow_decrease_exposure() {
	currenthdr->lumRange->decreaseExposure();
}
void MainGui::current_mdiwindow_extend_exposure() {
	currenthdr->lumRange->extendRange();
}
void MainGui::current_mdiwindow_fit_exposure() {
	currenthdr->lumRange->fitToDynamicRange();
}
void MainGui::current_mdiwindow_increase_exposure() {
	currenthdr->lumRange->increaseExposure();
}
void MainGui::current_mdiwindow_shrink_exposure() {
	currenthdr->lumRange->shrinkRange();
}
void MainGui::current_mdiwindow_ldr_exposure() {
	currenthdr->lumRange->lowDynamicRange();
}
void MainGui::current_mdiwindow_zoomin() {
	currenthdr->zoomIn();
	zoomOutAct->setEnabled(true);
	zoomInAct->setEnabled(currenthdr->getScaleFactor() < 3.0);
}
void MainGui::current_mdiwindow_zoomout() {
	currenthdr->zoomOut();
	zoomInAct->setEnabled(true);
	zoomOutAct->setEnabled(currenthdr->getScaleFactor() > 0.222);
}
void MainGui::current_mdiwindow_fit_to_win(bool checked) {
	currenthdr->fitToWindow(checked);
	zoomInAct->setEnabled(!checked);
	zoomOutAct->setEnabled(!checked);
	normalSizeAct->setEnabled(!checked);
}
void MainGui::current_mdiwindow_original_size() {
	currenthdr->normalSize();
	zoomInAct->setEnabled(true);
	zoomOutAct->setEnabled(true);
}

void MainGui::helpAbout() {
	QDialog *help=new QDialog();
	help->setAttribute(Qt::WA_DeleteOnClose);
	Ui::HelpDialog ui;
	ui.setupUi(help);
	QString docDir = QCoreApplication::applicationDirPath();
	docDir.append("/../Resources/html");
	ui.tb->setSearchPaths(QStringList("/usr/share/qtpfsgui/html") << "/usr/local/share/qtpfsgui/html" << "./html" << docDir << "/Applications/qtpfsgui.app/Contents/Resources/html");
	ui.tb->setSource(QUrl("index.html"));
	help->show();
}

void MainGui::updateRecentFileActions() {
	QStringList files = settings.value(KEY_RECENT_FILES).toStringList();
	
	int numRecentFiles = qMin(files.size(), (int)MaxRecentFiles);
	separatorRecentFiles->setVisible(numRecentFiles > 0);
	
	for (int i = 0; i < numRecentFiles; ++i) {
		QString text = QString("&%1 %2").arg(i + 1).arg(QFileInfo(files[i]).fileName());
		recentFileActs[i]->setText(text);
		recentFileActs[i]->setData(files[i]);
		recentFileActs[i]->setVisible(true);
	}
	for (int j = numRecentFiles; j < MaxRecentFiles; ++j)
		recentFileActs[j]->setVisible(false);
}

void MainGui::openRecentFile() {
	QAction *action = qobject_cast<QAction *>(sender());
	if (action) {
		if (! loadFile(action->data().toString()) ) {
			QStringList files = settings.value(KEY_RECENT_FILES).toStringList();
			files.removeAll(action->data().toString());
			settings.setValue(KEY_RECENT_FILES, files);
			updateRecentFileActions();
		}
	}
}

void MainGui::setCurrentFile(const QString &fileName) {
	QStringList files = settings.value(KEY_RECENT_FILES).toStringList();
	files.removeAll(fileName);
	files.prepend(fileName);
	while (files.size() > MaxRecentFiles)
		files.removeLast();

	settings.setValue(KEY_RECENT_FILES, files);
	updateRecentFileActions();
}

void MainGui::options_called() {
	unsigned int negcol=qtpfsgui_options->negcolor;
	unsigned int naninfcol=qtpfsgui_options->naninfcolor;
	QtpfsguiOptions *opts=new QtpfsguiOptions(this,qtpfsgui_options,&settings);
	opts->setAttribute(Qt::WA_DeleteOnClose);
	if (opts->exec() == QDialog::Accepted && (negcol!=qtpfsgui_options->negcolor || naninfcol!=qtpfsgui_options->naninfcolor) ) {
		QWidgetList allhdrs=workspace->windowList();
		foreach(QWidget *p,allhdrs) {
			((HdrViewer*)p)->update_colors(qtpfsgui_options->negcolor,qtpfsgui_options->naninfcolor);
		}
	}
}

void MainGui::transplant_called() {
	TransplantExifDialog *transplant=new TransplantExifDialog(this);
	transplant->setAttribute(Qt::WA_DeleteOnClose);
	transplant->exec();
}

// void MainGui::align_called() {
// 	AlignDialog *aligndialog=new AlignDialog(this);
// 	aligndialog->setAttribute(Qt::WA_DeleteOnClose);
// 	aligndialog->exec();
// }

void MainGui::load_options(qtpfsgui_opts *dest) {
	settings.beginGroup(GROUP_DCRAW);
		if (!settings.contains(KEY_AUTOWB))
			settings.setValue(KEY_AUTOWB,false);
		dest->dcraw_options.auto_wb=settings.value(KEY_AUTOWB,false).toBool();

		if (!settings.contains(KEY_CAMERAWB))
			settings.setValue(KEY_CAMERAWB,true);
		dest->dcraw_options.camera_wb=settings.value(KEY_CAMERAWB,true).toBool();

		if (!settings.contains(KEY_HIGHLIGHTS))
			settings.setValue(KEY_HIGHLIGHTS,0);
		dest->dcraw_options.highlights=settings.value(KEY_HIGHLIGHTS,0).toInt();

		if (!settings.contains(KEY_QUALITY))
			settings.setValue(KEY_QUALITY,2);
		dest->dcraw_options.quality=settings.value(KEY_QUALITY,2).toInt();

		if (!settings.contains(KEY_4COLORS))
			settings.setValue(KEY_4COLORS,false);
		dest->dcraw_options.four_colors=settings.value(KEY_4COLORS,false).toBool();

		if (!settings.contains(KEY_OUTCOLOR))
			settings.setValue(KEY_OUTCOLOR,4);
		dest->dcraw_options.output_color_space=settings.value(KEY_OUTCOLOR,4).toInt();
	settings.endGroup();

	settings.beginGroup(GROUP_HDRVISUALIZATION);
		if (!settings.contains(KEY_NANINFCOLOR))
			settings.setValue(KEY_NANINFCOLOR,0xFF000000);
		dest->naninfcolor=settings.value(KEY_NANINFCOLOR,0xFF000000).toUInt();
	
		if (!settings.contains(KEY_NEGCOLOR))
			settings.setValue(KEY_NEGCOLOR,0xFF000000);
		dest->negcolor=settings.value(KEY_NEGCOLOR,0xFF000000).toUInt();
	settings.endGroup();

	settings.beginGroup(GROUP_TONEMAPPING);
		if (!settings.contains(KEY_TEMP_RESULT_PATH))
			settings.setValue(KEY_TEMP_RESULT_PATH, QDir::currentPath());
		dest->tempfilespath=settings.value(KEY_TEMP_RESULT_PATH,QDir::currentPath()).toString();
	settings.endGroup();

	settings.beginGroup(GROUP_TIFF);
		if (!settings.contains(KEY_SAVE_LOGLUV))
			settings.setValue(KEY_SAVE_LOGLUV,true);
		dest->saveLogLuvTiff=settings.value(KEY_SAVE_LOGLUV,true).toBool();
	settings.endGroup();
}

MainGui::~MainGui() {
	delete qtpfsgui_options;
	for (int i = 0; i < MaxRecentFiles; ++i) {
		delete recentFileActs[i];
	}
	//separatorRecentFiles should not be required
}

void MainGui::fileExit() {
	QWidgetList allhdrs=workspace->windowList();
	bool closeok=true;
	foreach(QWidget *p,allhdrs) {
		if (((HdrViewer*)p)->NeedsSaving)
			closeok=false;
	}
#if QT_VERSION <= 0x040200
	if (closeok || (QMessageBox::warning(this,tr("Unsaved changes..."),tr("There is at least one Hdr with unsaved changes.<br>If you quit now, these changes will be lost."),
			QMessageBox::Ignore | QMessageBox::Default, QMessageBox::Cancel,QMessageBox::NoButton)
		== QMessageBox::Ignore))
#else
	if (closeok || (QMessageBox::warning(this,tr("Unsaved changes..."),tr("There is at least one Hdr with unsaved changes.<br>If you quit now, these changes will be lost."),
			QMessageBox::Discard | QMessageBox::Cancel,QMessageBox::Discard)
		== QMessageBox::Discard))
#endif
		emit close();
}

