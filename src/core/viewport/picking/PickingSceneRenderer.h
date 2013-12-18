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
 * \file PickingSceneRenderer.h
 * \brief Contains the definition of the Ovito::PickingSceneRenderer class.
 */
#ifndef __OVITO_PICKING_SCENE_RENDERER_H
#define __OVITO_PICKING_SCENE_RENDERER_H

#include <core/Core.h>
#include <core/rendering/viewport/ViewportSceneRenderer.h>

namespace Ovito {

/**
 * \brief A viewport renderer used for object picking.
 */
class OVITO_CORE_EXPORT PickingSceneRenderer : public ViewportSceneRenderer
{
public:

	struct ObjectRecord {
		quint32 baseObjectID;
		OORef<ObjectNode> objectNode;
		OORef<SceneObject> sceneObject;
		OORef<DisplayObject> displayObject;
	};

public:

	/// Constructor.
	PickingSceneRenderer(DataSet* dataset) : ViewportSceneRenderer(dataset) {
		setPicking(true);
	}

	/// This method is called just before renderFrame() is called.
	virtual void beginFrame(TimePoint time, const ViewProjectionParameters& params, Viewport* vp) override;

	/// Renders the current animation frame.
	virtual bool renderFrame(FrameBuffer* frameBuffer, QProgressDialog* progress) override;

	/// This method is called after renderFrame() has been called.
	virtual void endFrame() override;

	/// When picking mode is active, this registers an object being rendered.
	virtual quint32 beginPickObject(ObjectNode* objNode, SceneObject* sceneObj, DisplayObject* displayObj) override;

	/// Registers a range of sub-IDs belonging to the current object being rendered.
	virtual quint32 registerSubObjectIDs(quint32 subObjectCount) override;

	/// Call this when rendering of a pickable object is finished.
	virtual void endPickObject() override;

	/// Returns the object record and the sub-object ID for the object at the given pixel coordinates.
	std::tuple<const ObjectRecord*, quint32> objectAtLocation(const QPoint& pos) const;

	/// Given an object ID, looks up the corresponding record.
	const ObjectRecord* lookupObjectRecord(quint32 objectID) const;

	/// Returns the world space position corresponding to the given screen position.
	Point3 worldPositionFromLocation(const QPoint& pos) const;

	/// Resets the internal state of the picking renderer and clears the stored object records.
	void reset();

private:

	/// The OpenGL framebuffer.
	QScopedPointer<QOpenGLFramebufferObject> _framebufferObject;

	/// The next available object ID.
	ObjectRecord _currentObject;

	/// The list of registered objects.
	std::vector<ObjectRecord> _objects;

	/// The image containing the object information.
	QImage _image;

	/// The depth buffer data.
	std::unique_ptr<GLfloat[]> _depthBuffer;

	Q_OBJECT
	OVITO_OBJECT
};

};

#endif // __OVITO_PICKING_SCENE_RENDERER_H
