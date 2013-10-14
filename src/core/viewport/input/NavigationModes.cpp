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
#include <core/viewport/Viewport.h>
#include <core/viewport/ViewportManager.h>
#include <core/viewport/input/ViewportInputManager.h>
#include <core/viewport/input/NavigationModes.h>
#include <core/viewport/ViewportSettings.h>
#include <core/dataset/DataSetManager.h>
#include <core/animation/AnimManager.h>
#include <core/rendering/viewport/ViewportSceneRenderer.h>
#include <core/gui/mainwin/MainWindow.h>

namespace Ovito {

// Indicates around which point the camera should orbit.
NavigationMode::OrbitCenterMode NavigationMode::_orbitCenterMode = NavigationMode::ORBIT_SELECTION_CENTER;

// The user-defined orbiting center.
Point3 NavigationMode::_userOrbitCenter = Point3::Origin();

// The geometry buffer used to render the orbit center.
OORef<ArrowGeometryBuffer> NavigationMode::_orbitCenterMarker;

/******************************************************************************
* This is called by the system after the input handler is
* no longer the active handler.
******************************************************************************/
void NavigationMode::deactivated()
{
	if(_viewport) {
		// Restore old settings.
		_viewport->setCameraPosition(_oldCameraPosition);
		_viewport->setCameraDirection(_oldCameraDirection);
		_viewport->setFieldOfView(_oldFieldOfView);
		_viewport = NULL;
	}
	ViewportInputHandler::deactivated();
}

/******************************************************************************
* Handles the mouse down event for the given viewport.
******************************************************************************/
void NavigationMode::mousePressEvent(Viewport* vp, QMouseEvent* event)
{
	if(event->button() == Qt::RightButton && ViewportInputManager::instance().currentHandler() == this) {
		ViewportInputHandler::mousePressEvent(vp, event);
		return;
	}

	_viewport = vp;
	_startPoint = event->pos();
	_oldCameraPosition = vp->cameraPosition();
	_oldCameraDirection = vp->cameraDirection();
	_oldFieldOfView = vp->fieldOfView();
	_oldViewMatrix = vp->viewMatrix();
	_oldInverseViewMatrix = vp->inverseViewMatrix();
}

/******************************************************************************
* Handles the mouse up event for the given viewport.
******************************************************************************/
void NavigationMode::mouseReleaseEvent(Viewport* vp, QMouseEvent* event)
{
	if(_viewport) {
		_viewport = NULL;
	}
}

/******************************************************************************
* Handles the mouse move event for the given viewport.
******************************************************************************/
void NavigationMode::mouseMoveEvent(Viewport* vp, QMouseEvent* event)
{
	if(_viewport == vp) {
#if 1
		// Take the current mouse cursor position to make the navigation mode
		// look more responsive. The cursor position recorded when the mouse event was
		// generates may be too old.
		QPoint pos = vp->widget()->mapFromGlobal(QCursor::pos());
#else
		QPoint pos = event->pos();
#endif
		modifyView(vp, pos - _startPoint);

		// Force immediate viewport update.
		ViewportManager::instance().processViewportUpdates();
	}
}

/******************************************************************************
* Changes the way the center of rotation is chosen.
******************************************************************************/
void NavigationMode::setOrbitCenterMode(NavigationMode::OrbitCenterMode mode)
{
	if(_orbitCenterMode == mode) return;
	_orbitCenterMode = mode;
	ViewportManager::instance().updateViewports();
}

/******************************************************************************
* Sets the world space point around which the camera orbits.
******************************************************************************/
void NavigationMode::setUserOrbitCenter(const Point3& center)
{
	if(_userOrbitCenter == center) return;
	_userOrbitCenter = center;
	ViewportManager::instance().updateViewports();
}

/******************************************************************************
* Returns the world space point around which the camera orbits.
******************************************************************************/
Point3 NavigationMode::orbitCenter()
{
	// Update orbiting center.
	if(orbitCenterMode() == ORBIT_CONSTRUCTION_PLANE) {
		Box3 sceneBoundingBox = DataSetManager::instance().currentSet()->sceneRoot()->worldBoundingBox(AnimManager::instance().time());
		if(!sceneBoundingBox.isEmpty())
			return sceneBoundingBox.center();
	}
	else if(orbitCenterMode() == ORBIT_SELECTION_CENTER) {
		Box3 selectionBoundingBox;
		for(SceneNode* node : DataSetManager::instance().currentSelection()->nodes()) {
			selectionBoundingBox.addBox(node->worldBoundingBox(AnimManager::instance().time()));
		}
		if(!selectionBoundingBox.isEmpty())
			return selectionBoundingBox.center();
		else {
			Box3 sceneBoundingBox = DataSetManager::instance().currentSet()->sceneRoot()->worldBoundingBox(AnimManager::instance().time());
			if(!sceneBoundingBox.isEmpty())
				return sceneBoundingBox.center();
		}
	}
	else if(orbitCenterMode() == ORBIT_USER_DEFINED) {
		return _userOrbitCenter;
	}
	return Point3::Origin();
}

/******************************************************************************
* Lets the input mode render its overlay content in a viewport.
******************************************************************************/
void NavigationMode::renderOverlay(Viewport* vp, ViewportSceneRenderer* renderer, bool isActive)
{
	if(renderer->isPicking())
		return;

	// Render center of rotation.
	Point3 center = orbitCenter();
	FloatType symbolSize = vp->nonScalingSize(center);
	renderer->setWorldTransform(AffineTransformation::translation(center - Point3::Origin()) * AffineTransformation::scaling(symbolSize));

	// Create line buffer.
	if(!_orbitCenterMarker || !_orbitCenterMarker->isValid(renderer)) {
		_orbitCenterMarker = renderer->createArrowGeometryBuffer(ArrowGeometryBuffer::CylinderShape, ArrowGeometryBuffer::NormalShading, ArrowGeometryBuffer::HighQuality);
		_orbitCenterMarker->startSetElements(3);
		_orbitCenterMarker->setElement(0, {-1,0,0}, {2,0,0}, {1,0,0}, 0.05f);
		_orbitCenterMarker->setElement(1, {0,-1,0}, {0,2,0}, {0,1,0}, 0.05f);
		_orbitCenterMarker->setElement(2, {0,0,-1}, {0,0,2}, {0.2,0.2,1}, 0.05f);
		_orbitCenterMarker->endSetElements();
	}
	_orbitCenterMarker->render(renderer);
}

/******************************************************************************
* Computes the bounding box of the visual viewport overlay rendered by the input mode.
******************************************************************************/
Box3 NavigationMode::overlayBoundingBox(Viewport* vp, ViewportSceneRenderer* renderer, bool isActive)
{
	Point3 center = orbitCenter();
	FloatType symbolSize = vp->nonScalingSize(center);
	return Box3(center, symbolSize);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////////// Pan Mode ///////////////////////////////////

/******************************************************************************
* Computes the new view matrix based on the new mouse position.
******************************************************************************/
void PanMode::modifyView(Viewport* vp, const QPointF& delta)
{
	FloatType scaling;
	if(vp->isPerspectiveProjection())
		scaling = 10.0f * vp->nonScalingSize(OrbitMode::instance()->orbitCenter()) / vp->size().height();
	else
		scaling = 2.0f * _oldFieldOfView * vp->viewportWindow()->devicePixelRatio() / vp->size().height();
	FloatType deltaX = -scaling * delta.x();
	FloatType deltaY =  scaling * delta.y();
	Vector3 displacement = _oldInverseViewMatrix * Vector3(deltaX, deltaY, 0);
	vp->setCameraPosition(_oldCameraPosition + displacement);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// Zoom Mode ///////////////////////////////////

/******************************************************************************
* Computes the new view matrix based on the new mouse position.
******************************************************************************/
void ZoomMode::modifyView(Viewport* vp, const QPointF& delta)
{
	if(vp->isPerspectiveProjection()) {
		FloatType amount =  -5.0 * sceneSizeFactor() * delta.y();
		vp->setCameraPosition(_oldCameraPosition + _oldCameraDirection.resized(amount));
	}
	else {
		FloatType scaling = (FloatType)exp(0.003 * delta.y());
		vp->setFieldOfView(_oldFieldOfView * scaling);
	}
}

/******************************************************************************
* Computes a scaling factor that depends on the total size of the scene which is used to
* control the zoom sensitivity in perspective mode.
******************************************************************************/
FloatType ZoomMode::sceneSizeFactor()
{
	Box3 sceneBoundingBox = DataSetManager::instance().currentSet()->sceneRoot()->worldBoundingBox(AnimManager::instance().time());
	if(!sceneBoundingBox.isEmpty())
		return sceneBoundingBox.size().length() * 5e-4;
	else
		return 0.1;
}

/******************************************************************************
* Zooms the viewport in or out.
******************************************************************************/
void ZoomMode::zoom(Viewport* vp, FloatType steps)
{
	if(vp->isPerspectiveProjection()) {
		vp->setCameraPosition(vp->cameraPosition() + vp->cameraDirection().resized(sceneSizeFactor() * steps));
	}
	else {
		vp->setFieldOfView(vp->fieldOfView() * exp(-steps * 0.001));
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// FOV Mode ///////////////////////////////////

/******************************************************************************
* Computes the new field of view based on the new mouse position.
******************************************************************************/
void FOVMode::modifyView(Viewport* vp, const QPointF& delta)
{
	if(vp->isPerspectiveProjection()) {
		FloatType newFOV = _oldFieldOfView + (FloatType)delta.y() * 0.002;
		newFOV = std::max(newFOV, (FloatType)(5.0 * FLOATTYPE_PI / 180.0));
		newFOV = std::min(newFOV, (FloatType)(170.0 * FLOATTYPE_PI / 180.0));
		vp->setFieldOfView(newFOV);
	}
	else {
		FloatType scaling = (FloatType)exp(0.006 * delta.y());
		vp->setFieldOfView(_oldFieldOfView * scaling);
	}
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// Orbit Mode ///////////////////////////////////

/******************************************************************************
* Computes the new view matrix based on the new mouse position.
******************************************************************************/
void OrbitMode::modifyView(Viewport* vp, const QPointF& delta)
{
	if(!vp->isPerspectiveProjection())
		vp->setViewType(Viewport::VIEW_ORTHO);

	Matrix3 coordSys = ViewportSettings::getSettings().coordinateSystemOrientation();
	Vector3 v = coordSys.inverse() * -_oldCameraDirection;

	FloatType theta, phi;
	if(v.x() == 0 && v.y() == 0)
		theta = FLOATTYPE_PI;
	else
		theta = atan2(v.x(), v.y());
	phi = atan2(sqrt(v.x() * v.x() + v.y() * v.y()), v.z());

	FloatType speed = 4.0 / vp->size().height();
	FloatType deltaTheta = speed * delta.x();
	FloatType deltaPhi = -speed * delta.y();
	if(phi + deltaPhi < FLOATTYPE_EPSILON)
		deltaPhi = -phi + FLOATTYPE_EPSILON;
	else if(phi + deltaPhi > FLOATTYPE_PI - FLOATTYPE_EPSILON)
		deltaPhi = FLOATTYPE_PI - FLOATTYPE_EPSILON - phi;

	AffineTransformation oldViewMatrix = AffineTransformation::lookAlong(_oldCameraPosition, _oldCameraDirection, ViewportSettings::getSettings().upVector());
	Vector3 t = (oldViewMatrix * orbitCenter()) - Point3::Origin();
	AffineTransformation newViewMatrix =
		AffineTransformation::translation(t) *
		AffineTransformation::rotationX(-deltaPhi) *
		AffineTransformation::translation(-t) *
		oldViewMatrix *
		AffineTransformation::translation(orbitCenter() - Point3::Origin()) *
		AffineTransformation::rotation(Rotation(ViewportSettings::getSettings().upVector(), deltaTheta)) *
		AffineTransformation::translation(-(orbitCenter() - Point3::Origin()));

	vp->setCameraDirection(newViewMatrix.inverse() * Vector3(0,0,-1));
	vp->setCameraPosition(Point3::Origin() + newViewMatrix.inverse().translation());
}

/////////////////////////////////////////////////////////////////////////////////////
///////////////////////////// Pick Orbit Center Mode ////////////////////////////////

/******************************************************************************
* Sets the orbit rotation center to the space location under given mouse coordinates.
******************************************************************************/
bool PickOrbitCenterMode::pickOrbitCenter(Viewport* vp, const QPointF& pos)
{
	Point3 p;
	if(findIntersection(vp, pos, p)) {
		NavigationMode::setOrbitCenterMode(NavigationMode::ORBIT_USER_DEFINED);
		NavigationMode::setUserOrbitCenter(p);
		return true;
	}
	else {
		NavigationMode::setOrbitCenterMode(NavigationMode::ORBIT_SELECTION_CENTER);
		NavigationMode::setUserOrbitCenter(Point3::Origin());
		MainWindow::instance().statusBar()->showMessage(tr("No object has been picked. Resetting orbit center to default position."), 1200);
		return false;
	}
}

/******************************************************************************
* Handles the mouse down events for a Viewport.
******************************************************************************/
void PickOrbitCenterMode::mousePressEvent(Viewport* vp, QMouseEvent* event)
{
	if(event->button() == Qt::LeftButton) {
		if(pickOrbitCenter(vp, event->localPos()))
			return;
	}
	ViewportInputHandler::mousePressEvent(vp, event);
}

/******************************************************************************
* Is called when the user moves the mouse while the operation is not active.
******************************************************************************/
void PickOrbitCenterMode::mouseMoveEvent(Viewport* vp, QMouseEvent* event)
{
	ViewportInputHandler::mouseMoveEvent(vp, event);

	Point3 p;
	bool isOverObject = findIntersection(vp, event->localPos(), p);

	if(!isOverObject && _showCursor) {
		_showCursor = false;
		setCursor(QCursor());
	}
	else if(isOverObject && !_showCursor) {
		_showCursor = true;
		setCursor(_hoverCursor);
	}
}

/******************************************************************************
* Finds the closest intersection point between a ray originating from the
* current mouse cursor position and the whole scene.
******************************************************************************/
bool PickOrbitCenterMode::findIntersection(Viewport* vp, const QPointF& mousePos, Point3& intersectionPoint)
{
	ViewportPickResult pickResults = vp->pick(mousePos);
	if(!pickResults.valid)
		return false;

	intersectionPoint = pickResults.worldPosition;
	return true;
}

};
