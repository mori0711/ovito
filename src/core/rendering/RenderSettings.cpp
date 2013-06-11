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
#include <core/rendering/RenderSettings.h>
#include <core/rendering/RenderSettingsEditor.h>
#include <core/rendering/SceneRenderer.h>
#include <core/rendering/viewport/ViewportSceneRenderer.h>
#include <core/viewport/Viewport.h>

namespace Ovito {

IMPLEMENT_SERIALIZABLE_OVITO_OBJECT(RenderSettings, RefTarget)
SET_OVITO_OBJECT_EDITOR(RenderSettings, RenderSettingsEditor)
DEFINE_REFERENCE_FIELD(RenderSettings, _renderer, "Renderer", SceneRenderer)
DEFINE_REFERENCE_FIELD(RenderSettings, _backgroundColor, "BackgroundColor", VectorController)
DEFINE_PROPERTY_FIELD(RenderSettings, _outputImageWidth, "OutputImageWidth")
DEFINE_PROPERTY_FIELD(RenderSettings, _outputImageHeight, "OutputImageHeight")
DEFINE_PROPERTY_FIELD(RenderSettings, _generateAlphaChannel, "GenerateAlphaChannel")
DEFINE_PROPERTY_FIELD(RenderSettings, _saveToFile, "SaveToFile")
DEFINE_PROPERTY_FIELD(RenderSettings, _skipExistingImages, "SkipExistingImages")
DEFINE_PROPERTY_FIELD(RenderSettings, _renderingRangeType, "RenderingRangeType")
DEFINE_PROPERTY_FIELD(RenderSettings, _customRangeStart, "CustomRangeStart")
DEFINE_PROPERTY_FIELD(RenderSettings, _customRangeEnd, "CustomRangeEnd")
DEFINE_PROPERTY_FIELD(RenderSettings, _everyNthFrame, "EveryNthFrame")
DEFINE_PROPERTY_FIELD(RenderSettings, _fileNumberBase, "FileNumberBase")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _renderer, "Renderer")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _backgroundColor, "Background color")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _outputImageWidth, "Width")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _outputImageHeight, "Height")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _generateAlphaChannel, "Make background transparent")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _saveToFile, "Save to file")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _skipExistingImages, "Skip existing animation images")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _renderingRangeType, "Rendering range")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _customRangeStart, "Range start")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _customRangeEnd, "Range end")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _everyNthFrame, "Every Nth frame")
SET_PROPERTY_FIELD_LABEL(RenderSettings, _fileNumberBase, "File number base")

/******************************************************************************
* Constructor.
* Creates an instance of the default renderer class which can be 
* accessed via the renderer() method.
******************************************************************************/
RenderSettings::RenderSettings() :
	_outputImageWidth(640), _outputImageHeight(480), _generateAlphaChannel(false),
	_saveToFile(false), _skipExistingImages(false), _renderingRangeType(CURRENT_FRAME),
	_customRangeStart(0), _customRangeEnd(100), _everyNthFrame(1), _fileNumberBase(0)
{
	INIT_PROPERTY_FIELD(RenderSettings::_renderer);
	INIT_PROPERTY_FIELD(RenderSettings::_backgroundColor);
	INIT_PROPERTY_FIELD(RenderSettings::_outputImageWidth);
	INIT_PROPERTY_FIELD(RenderSettings::_outputImageHeight);
	INIT_PROPERTY_FIELD(RenderSettings::_generateAlphaChannel);
	INIT_PROPERTY_FIELD(RenderSettings::_saveToFile);
	INIT_PROPERTY_FIELD(RenderSettings::_skipExistingImages);
	INIT_PROPERTY_FIELD(RenderSettings::_renderingRangeType);
	INIT_PROPERTY_FIELD(RenderSettings::_customRangeStart);
	INIT_PROPERTY_FIELD(RenderSettings::_customRangeEnd);
	INIT_PROPERTY_FIELD(RenderSettings::_everyNthFrame);
	INIT_PROPERTY_FIELD(RenderSettings::_fileNumberBase);

	// Setup default background color.
	_backgroundColor = ControllerManager::instance().createDefaultController<VectorController>();
	setBackgroundColor(Color(1,1,1));

	// Create an instance of the default renderer class.
	setRendererClass(&ViewportSceneRenderer::OOType);
}

/******************************************************************************
* Returns the class of the current renderer or NULL if there is no current renderer. 
******************************************************************************/
const OvitoObjectType* RenderSettings::rendererClass() const
{
	return renderer() ? &renderer()->getOOType() : nullptr;
}

/******************************************************************************
* Selects the type of renderer to use for rendering. The specified class must be derived from PluginRenderer. 
* This method will create a new instance of the given renderer class and stores the new renderer in this settings object.
* When an error occurs an exception is thrown. 
******************************************************************************/
void RenderSettings::setRendererClass(const OvitoObjectType* rendererClass)
{
	OVITO_ASSERT(rendererClass != NULL);
	OVITO_ASSERT(rendererClass->isDerivedFrom(SceneRenderer::OOType));
	
	// Create a new instance of the specified class.
	OORef<SceneRenderer> newRenderer = static_object_cast<SceneRenderer>(rendererClass->createInstance());
#if 0
	newRenderer->_renderSettings = this;
#endif
	
	// Make the new renderer the current renderer.
	_renderer = newRenderer;
}

/******************************************************************************
* Sets the output filename of the rendered image. 
******************************************************************************/
void RenderSettings::setImageFilename(const QString& filename)
{
	if(filename == imageFilename()) return;
	_imageInfo.setFilename(filename);
	notifyDependents(ReferenceEvent::TargetChanged);
}

/******************************************************************************
* Sets the output image info of the rendered image.
******************************************************************************/
void RenderSettings::setImageInfo(const ImageInfo& imageInfo)
{
	if(imageInfo == _imageInfo) return;
	_imageInfo = imageInfo;
	notifyDependents(ReferenceEvent::TargetChanged);
}

#define RENDER_SETTINGS_FILE_FORMAT_VERSION		1

/******************************************************************************
* Saves the class' contents to the given stream. 
******************************************************************************/
void RenderSettings::saveToStream(ObjectSaveStream& stream)
{
	RefTarget::saveToStream(stream);

	stream.beginChunk(RENDER_SETTINGS_FILE_FORMAT_VERSION);
	stream << _imageInfo;
	stream.endChunk();
}

/******************************************************************************
* Loads the class' contents from the given stream. 
******************************************************************************/
void RenderSettings::loadFromStream(ObjectLoadStream& stream)
{
	RefTarget::loadFromStream(stream);

	int fileVersion = stream.expectChunkRange(0, RENDER_SETTINGS_FILE_FORMAT_VERSION);
	if(fileVersion == 0) {
		bool generateAlphaChannel;
		RenderingRangeType renderingRange;
		stream.readEnum(renderingRange);
		stream >> _imageInfo;
		stream >> generateAlphaChannel;
		_generateAlphaChannel = generateAlphaChannel;
		_renderingRangeType = renderingRange;
		_outputImageWidth = _imageInfo.imageWidth();
		_outputImageHeight = _imageInfo.imageHeight();
	}
	else {
		stream >> _imageInfo;
	}
	stream.closeChunk();
#if 0
	if(renderer()) renderer()->_renderSettings = this;
#endif
}

/******************************************************************************
* Creates a copy of this object. 
******************************************************************************/
OORef<RefTarget> RenderSettings::clone(bool deepCopy, CloneHelper& cloneHelper)
{
	// Let the base class create an instance of this class.
	OORef<RenderSettings> clone = static_object_cast<RenderSettings>(RefTarget::clone(deepCopy, cloneHelper));
	
	/// Copy data values.
	clone->_imageInfo = this->_imageInfo;
	
	/// Copy renderer.
	OVITO_ASSERT((clone->renderer() != NULL) == (renderer() != NULL));
#if 0
	if(clone->renderer()) renderer()->_renderSettings = clone.get();
#endif

	return clone;
}

};