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

#include <core/Core.h>
#include <core/scene/SceneNode.h>
#include <core/scene/SceneRoot.h>
#include <core/scene/ObjectNode.h>
#include <core/scene/GroupNode.h>
#include <core/scene/pipeline/PipelineObject.h>
#include <core/scene/pipeline/Modifier.h>
#include <core/scene/display/DisplayObject.h>
#include "ViewportSceneRenderer.h"

namespace Ovito {

IMPLEMENT_SERIALIZABLE_OVITO_OBJECT(ViewportSceneRenderer, SceneRenderer);

/******************************************************************************
* Renders the current animation frame.
******************************************************************************/
void ViewportSceneRenderer::renderFrame()
{
	OVITO_CHECK_OBJECT_POINTER(viewport());
	_glcontext = viewport()->_glcontext;
	glMatrixMode(GL_MODELVIEW);

	renderScene();

	_glcontext = nullptr;
}

/******************************************************************************
* Changes the current local to world transformation matrix.
******************************************************************************/
void ViewportSceneRenderer::setWorldTransform(const AffineTransformation& tm)
{
	Matrix4 localToViewTM(projParams().viewMatrix * tm);
	glLoadMatrix(localToViewTM);
}

/******************************************************************************
* Translates an OpenGL error code to a human-readable message string.
******************************************************************************/
const char* ViewportSceneRenderer::openglErrorString(GLenum errorCode)
{
	switch(errorCode) {
	case GL_NO_ERROR: return "GL_NO_ERROR - No error has been recorded.";
	case GL_INVALID_ENUM: return "GL_INVALID_ENUM - An unacceptable value is specified for an enumerated argument.";
	case GL_INVALID_VALUE: return "GL_INVALID_VALUE - A numeric argument is out of range.";
	case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION - The specified operation is not allowed in the current state.";
	case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW - This command would cause a stack overflow.";
	case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW - This command would cause a stack underflow.";
	case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY - There is not enough memory left to execute the command.";
	case GL_TABLE_TOO_LARGE: return "GL_TABLE_TOO_LARGE - The specified table exceeds the implementation's maximum supported table size.";
	default: return "Unknown OpenGL error code.";
	}
}

/******************************************************************************
* Renders a single node.
******************************************************************************/
void ViewportSceneRenderer::renderNode(SceneNode* node)
{
    OVITO_CHECK_OBJECT_POINTER(node);

    // Setup transformation matrix.
	TimeInterval interval;
	const AffineTransformation& nodeTM = node->getWorldTransform(time(), interval);
	setWorldTransform(nodeTM);

	if(node->isObjectNode()) {
		ObjectNode* objNode = static_object_cast<ObjectNode>(node);

		// Do not render node if it is the view node of the viewport or
		// if it is the target of the view node.
		if(viewport() && viewport()->viewNode()) {
			if(viewport()->viewNode() == objNode || viewport()->viewNode()->targetNode() == objNode)
				return;
		}

		// Evaluate geometry pipeline of object node and render the results.
		objNode->render(time(), this);

		// Render the modifiers applied to the object.
		PipelineObject* pipelineObj = dynamic_object_cast<PipelineObject>(objNode->sceneObject());
		renderPipelineObject(pipelineObj, objNode);
	}

	// Continue with rendering the child nodes.
	SceneRenderer::renderNode(node);
}

/******************************************************************************
* Renders the selected modifiers.
******************************************************************************/
void ViewportSceneRenderer::renderPipelineObject(PipelineObject* pipelineObj, ObjectNode* objNode)
{
	if(!pipelineObj) return;

#if 0
	// Retrieve the modifier that is currently selected.
	Modifier* selectedModifier = dynamic_object_cast<Modifier>(MAIN_FRAME->commandPanel()->editObject());
	if(!selectedModifier) return;

	// Iterate over all modifier applications until the selected modifier is found.
	// Then render it.
	Q_FOREACH(ModifierApplication* modApp, modObj->modifierApplications()) {
		if(modApp->modifier() == selectedModifier) {

			// Setup transformation.
			TimeInterval iv;
			const AffineTransformation& nodeTM = objNode->getWorldTransform(currentTime, iv);
			viewport->setWorldMatrix(objNode->objectTransform() * nodeTM);

			// Render selected modifier.
			selectedModifier->renderModifier(currentTime, objNode, modApp, viewport);

			return;
		}
	}

	// Not found yet -> go on with nested modified objects.
	for(int i = 0; i < modObj->inputObjectCount(); i++)
		renderModifiedObject(dynamic_object_cast<ModifiedObject>(modObj->inputObject(i)), objNode);
#endif
}

};