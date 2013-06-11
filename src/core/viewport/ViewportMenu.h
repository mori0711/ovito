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

#include <core/viewport/Viewport.h>
#include "ViewportWindow.h"

namespace Ovito {

/**
 * \brief The context menu of the viewports.
 */ 
class ViewportMenu : public QMenu
{
	Q_OBJECT
	
public:

	/// Initializes the menu.
	ViewportMenu(Viewport* vp);

	/// Displays the menu.
	void show(const QPoint& pos);

private Q_SLOTS:

	void onShowGrid(bool checked);
	void onShowRenderFrame(bool checked);
	void onViewType(QAction* action);
	void onWindowFocusChanged() {
		if(QGuiApplication::focusWindow() && QGuiApplication::focusWindow()->flags().testFlag(Qt::Popup) == false)
			hide();
	}
	
private:
    
	/// The viewport this menu belongs to.
	Viewport* viewport;
	
	/// The view type sub-menu.
	QMenu* viewTypeMenu;
	
	/// The menu group that lists all cameras.
	QActionGroup* viewNodeGroup;
};


};