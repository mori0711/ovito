///////////////////////////////////////////////////////////////////////////////
// 
//  Copyright (2013) Alexander Stukowski
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  OVITO is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
///////////////////////////////////////////////////////////////////////////////

/** 
 * \file ImportFileDialog.h
 * \brief Contains the definition of the Ovito::ImportFileDialog class.
 */

#ifndef __OVITO_IMPORT_FILE_DIALOG_H
#define __OVITO_IMPORT_FILE_DIALOG_H

#include <core/Core.h>
#include <core/dataset/importexport/FileImporter.h>
#include "HistoryFileDialog.h"

namespace Ovito {

/**
 * \brief This file chooser dialog lets the user select a file to be imported.
 */
class ImportFileDialog : public HistoryFileDialog
{
	Q_OBJECT
	
public:

	/// \brief Constructs the dialog window.
	ImportFileDialog(QWidget* parent = nullptr, const QString& caption = QString());

	/// \brief Returns the file to import after the dialog has been closed with "OK".
	QString fileToImport() const;

	/// \brief After the dialog has been closed with "OK", this method creates a parser object for the selected file.
	OORef<FileImporter> createFileImporter();

#ifdef Q_WS_MAC
	/// Shows the dialog box.
	virtual int exec() override;
#endif

private Q_SLOTS:

	/// This is called when the user has pressed the OK button of the dialog box.
	void onFileSelected(const QString& file);

private:

	QStringList _filterStrings;
};

};

#endif // __OVITO_IMPORT_FILE_DIALOG_H