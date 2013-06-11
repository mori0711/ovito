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
 * \file RefTargetListener.h
 * \brief Contains the definition of the Ovito::RefTargetListener class.
 */

#ifndef __OVITO_REFTARGET_LISTENER_H
#define __OVITO_REFTARGET_LISTENER_H

#include <core/Core.h>
#include "RefTarget.h"

namespace Ovito {

/**
 * \brief A helper class that can be used to monitor the notification events
 *        generated by a RefTarget object without the need to write a new RefMaker derived class.
 *
 * This class is designed to be used on the stack or as a member of another
 * class that is not derived from RefMaker but still wants to receive notification events
 * from a RefTarget.
 */
class RefTargetListener : public RefMaker
{
public:

	/// \brief The default constructor.
	RefTargetListener();

	/// \brief Destructor.
	virtual ~RefTargetListener();

	/// \brief Returns the current target this listener is listening to.
	/// \return The current target object or \c NULL.
	/// \sa setTarget()
	RefTarget* target() const { return _target; }

	/// \brief Sets the current target this listener should listen to.
	/// \param newTarget The new target or \c NULL.
	/// \sa target()
	void setTarget(RefTarget* newTarget) { _target = newTarget; }

Q_SIGNALS:

	/// \brief This Qt signal is emitted by the listener each time it receives a notification
	///        event from the current target.
	/// \param event The notification event.
	void notificationEvent(ReferenceEvent* event);

protected:

	/// \brief Deletes this object when it is no longer needed.
	virtual void autoDeleteObject() override;

	/// \brief Is called when the RefTarget referenced by this listener has generated an event.
	virtual bool referenceEvent(RefTarget* source, ReferenceEvent* event) override;

private:

	/// The RefTarget which is being monitored by this listener.
	ReferenceField<RefTarget> _target;

	Q_OBJECT
	OVITO_OBJECT

	DECLARE_REFERENCE_FIELD(_target);
};

};

#endif // __OVITO_REFTARGET_LISTENER_H
