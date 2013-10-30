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

#ifndef __OVITO_CAMERA_OBJECT_H
#define __OVITO_CAMERA_OBJECT_H

#include <core/Core.h>
#include <core/scene/objects/camera/AbstractCameraObject.h>
#include <core/animation/controller/Controller.h>
#include <core/gui/properties/PropertiesEditor.h>

namespace Ovito {

/**
 * The default camera scene object.
 */
class OVITO_CORE_EXPORT CameraObject : public AbstractCameraObject
{
public:

	/// Default constructor.
	Q_INVOKABLE CameraObject();

	/// \brief Returns the title of this object.
	virtual QString objectTitle() override { return tr("Camera"); }

	/// Returns whether this camera uses a perspective or an orthogonal projection.
	bool isPerspective() const { return _isPerspective; }

	/// Sets whether this camera uses a perspective or an orthogonal projection.
	void setIsPerspective(bool perspective) { _isPerspective = perspective; }

	/// Returns the controller that controls the field-of-view angle of the camera with perspective projection.
	FloatController* fovController() const { return _fov; }

	/// Returns the controller that controls the zoom of the camera with orthogonal projection.
	FloatController* zoomController() const { return _zoom; }

	/// \brief Returns a structure describing the camera's projection.
	/// \param[in] time The animation time for which the camera's projection parameters should be determined.
	/// \param[in,out] projParams The structure that is to be filled with the projection parameters.
	///     The following fields of the ViewProjectionParameters structure are already filled in when the method is called:
	///   - ViewProjectionParameters::aspectRatio (The aspect ratio (height/width) of the viewport)
	///   - ViewProjectionParameters::viewMatrix (The world to view space transformation)
	///   - ViewProjectionParameters::boundingBox (The bounding box of the scene in world space coordinates)
	virtual void projectionParameters(TimePoint time, ViewProjectionParameters& projParams) override;

	/// \brief Returns the field of view of the camera.
	virtual FloatType fieldOfView(TimePoint time, TimeInterval& validityInterval) override;

	/// \brief Changes the field of view of the camera.
	virtual void setFieldOfView(TimePoint time, FloatType newFOV) override;

	/// Asks the object for its validity interval at the given time.
	virtual TimeInterval objectValidity(TimePoint time) override;

private:

	/// Determines if this camera uses a perspective projection.
	PropertyField<bool> _isPerspective;

	/// This controller stores the field of view of the camera if it uses a perspective projection.
	ReferenceField<FloatController> _fov;

	/// This controller stores the field of view of the camera if it uses an orthogonal projection.
	ReferenceField<FloatController> _zoom;

	Q_OBJECT
	OVITO_OBJECT

	DECLARE_PROPERTY_FIELD(_isPerspective);
	DECLARE_REFERENCE_FIELD(_fov);
	DECLARE_REFERENCE_FIELD(_zoom);
};


/**
 * A properties editor for the CameraObject class.
 */
class CameraObjectEditor : public PropertiesEditor
{
public:

	/// Default constructor.
	Q_INVOKABLE CameraObjectEditor() {}

protected:
	
	/// Creates the user interface controls for the editor.
	virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;
	
private:

	Q_OBJECT
	OVITO_OBJECT
};

};

#endif // __CAMERA_OBJECT_H