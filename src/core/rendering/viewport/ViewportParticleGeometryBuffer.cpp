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
#include "ViewportParticleGeometryBuffer.h"
#include "ViewportSceneRenderer.h"

/// The maximum resolution of the texture used for billboard rendering of particles. Specified as a power of two.
#define BILLBOARD_TEXTURE_LEVELS 	8

namespace Ovito {

/******************************************************************************
* Constructor.
******************************************************************************/
ViewportParticleGeometryBuffer::ViewportParticleGeometryBuffer(ViewportSceneRenderer* renderer, ShadingMode shadingMode, RenderingQuality renderingQuality, ParticleShape shape) :
	ParticleGeometryBuffer(shadingMode, renderingQuality, shape),
	_contextGroup(QOpenGLContextGroup::currentContextGroup()),
	_billboardTexture(0), _shader(nullptr), _pickingShader(nullptr),
	_usingGeometryShader(renderer->useGeometryShaders())
{
	OVITO_ASSERT(renderer->glcontext()->shareGroup() == _contextGroup);

	// Determine rendering technique to use.
	if(shadingMode == FlatShading) {
		if(renderer->usePointSprites())
			_renderingTechnique = POINT_SPRITES;
		else
			_renderingTechnique = IMPOSTER_QUADS;
	}
	else {
		if(shape == SphericalShape && renderingQuality < HighQuality) {
			if(renderer->usePointSprites())
				_renderingTechnique = POINT_SPRITES;
			else
				_renderingTechnique = IMPOSTER_QUADS;
		}
		else {
			_renderingTechnique = CUBE_GEOMETRY;
		}
	}

	// Load the right OpenGL shaders.
	if(_renderingTechnique == POINT_SPRITES) {
		if(shadingMode == FlatShading) {
			if(shape == SphericalShape) {
				_shader = renderer->loadShaderProgram("particle_pointsprite_spherical_flat",
						":/core/glsl/particles/pointsprites/sphere/without_depth.vs",
						":/core/glsl/particles/pointsprites/sphere/flat_shading.fs");
				_pickingShader = renderer->loadShaderProgram("particle_pointsprite_spherical_nodepth_picking",
						":/core/glsl/particles/pointsprites/sphere/picking/without_depth.vs",
						":/core/glsl/particles/pointsprites/sphere/picking/flat_shading.fs");
			}
			else if(shape == SquareShape) {
				_shader = renderer->loadShaderProgram("particle_pointsprite_square_flat",
						":/core/glsl/particles/pointsprites/sphere/without_depth.vs",
						":/core/glsl/particles/pointsprites/square/flat_shading.fs");
				_pickingShader = renderer->loadShaderProgram("particle_pointsprite_square_flat_picking",
						":/core/glsl/particles/pointsprites/sphere/picking/without_depth.vs",
						":/core/glsl/particles/pointsprites/square/picking/flat_shading.fs");
			}
		}
		else if(shadingMode == NormalShading) {
			if(shape == SphericalShape) {
				if(renderingQuality == LowQuality) {
					_shader = renderer->loadShaderProgram("particle_pointsprite_spherical_shaded_nodepth",
							":/core/glsl/particles/pointsprites/sphere/without_depth.vs",
							":/core/glsl/particles/pointsprites/sphere/without_depth.fs");
					_pickingShader = renderer->loadShaderProgram("particle_pointsprite_spherical_nodepth_picking",
							":/core/glsl/particles/pointsprites/sphere/picking/without_depth.vs",
							":/core/glsl/particles/pointsprites/sphere/picking/flat_shading.fs");
				}
				else if(renderingQuality == MediumQuality) {
					_shader = renderer->loadShaderProgram("particle_pointsprite_spherical_shaded_depth",
							":/core/glsl/particles/pointsprites/sphere/with_depth.vs",
							":/core/glsl/particles/pointsprites/sphere/with_depth.fs");
					_pickingShader = renderer->loadShaderProgram("particle_pointsprite_spherical_shaded_depth_picking",
							":/core/glsl/particles/pointsprites/sphere/picking/with_depth.vs",
							":/core/glsl/particles/pointsprites/sphere/picking/with_depth.fs");
				}
			}
		}
	}
	else if(_renderingTechnique == IMPOSTER_QUADS) {
		if(shadingMode == FlatShading) {
			if(shape == SphericalShape) {
				_shader = renderer->loadShaderProgram("particle_imposter_spherical_flat",
						":/core/glsl/particles/imposter/sphere/without_depth.vs",
						":/core/glsl/particles/imposter/sphere/flat_shading.fs");
				_pickingShader = renderer->loadShaderProgram("particle_imposter_spherical_nodepth_picking",
						":/core/glsl/particles/imposter/sphere/picking/without_depth.vs",
						":/core/glsl/particles/imposter/sphere/picking/flat_shading.fs");
			}
			else if(shape == SquareShape) {
				_shader = renderer->loadShaderProgram("particle_imposter_square_flat",
						":/core/glsl/particles/imposter/sphere/without_depth.vs",
						":/core/glsl/particles/pointsprites/square/flat_shading.fs");
				_pickingShader = renderer->loadShaderProgram("particle_imposter_square_flat_picking",
						":/core/glsl/particles/imposter/sphere/picking/without_depth.vs",
						":/core/glsl/particles/pointsprites/square/picking/flat_shading.fs");
			}
		}
		else if(shadingMode == NormalShading) {
			if(shape == SphericalShape) {
				if(renderingQuality == LowQuality) {
					_shader = renderer->loadShaderProgram("particle_imposter_spherical_shaded_nodepth",
							":/core/glsl/particles/imposter/sphere/without_depth.vs",
							":/core/glsl/particles/imposter/sphere/without_depth.fs");
					_pickingShader = renderer->loadShaderProgram("particle_imposter_spherical_nodepth_picking",
							":/core/glsl/particles/imposter/sphere/picking/without_depth.vs",
							":/core/glsl/particles/imposter/sphere/picking/flat_shading.fs");
				}
				else if(renderingQuality == MediumQuality) {
					_shader = renderer->loadShaderProgram("particle_imposter_spherical_shaded_depth",
							":/core/glsl/particles/imposter/sphere/with_depth.vs",
							":/core/glsl/particles/imposter/sphere/with_depth.fs");
					_pickingShader = renderer->loadShaderProgram("particle_imposter_spherical_shaded_depth_picking",
							":/core/glsl/particles/imposter/sphere/picking/with_depth.vs",
							":/core/glsl/particles/imposter/sphere/picking/with_depth.fs");
				}
			}
		}
	}
	else if(_renderingTechnique == CUBE_GEOMETRY) {
		if(shadingMode == NormalShading) {
			if(renderer->useGeometryShaders()) {
				if(shape == SphericalShape && renderingQuality == HighQuality) {
					_shader = renderer->loadShaderProgram("particle_geomshader_sphere",
							":/core/glsl/particles/geometry/sphere/sphere.vs",
							":/core/glsl/particles/geometry/sphere/sphere.fs",
							":/core/glsl/particles/geometry/sphere/sphere.gs");
					_pickingShader = renderer->loadShaderProgram("particle_geomshader_sphere_picking",
							":/core/glsl/particles/geometry/sphere/picking/sphere.vs",
							":/core/glsl/particles/geometry/sphere/picking/sphere.fs",
							":/core/glsl/particles/geometry/sphere/picking/sphere.gs");
				}
				else if(shape == SquareShape) {
					_shader = renderer->loadShaderProgram("particle_geomshader_cube",
							":/core/glsl/particles/geometry/cube/cube.vs",
							":/core/glsl/particles/geometry/cube/cube.fs",
							":/core/glsl/particles/geometry/cube/cube.gs");
					_pickingShader = renderer->loadShaderProgram("particle_geomshader_cube_picking",
							":/core/glsl/particles/geometry/cube/picking/cube.vs",
							":/core/glsl/particles/geometry/cube/picking/cube.fs",
							":/core/glsl/particles/geometry/cube/picking/cube.gs");
				}
			}
			else {
				if(shape == SphericalShape && renderingQuality == HighQuality) {
					_shader = renderer->loadShaderProgram("particle_tristrip_sphere",
							":/core/glsl/particles/geometry/sphere/sphere_tristrip.vs",
							":/core/glsl/particles/geometry/sphere/sphere.fs");
					_pickingShader = renderer->loadShaderProgram("particle_tristrip_sphere_picking",
							":/core/glsl/particles/geometry/sphere/picking/sphere_tristrip.vs",
							":/core/glsl/particles/geometry/sphere/picking/sphere.fs");
				}
				else if(shape == SquareShape) {
					_shader = renderer->loadShaderProgram("particle_tristrip_cube",
							":/core/glsl/particles/geometry/cube/cube_tristrip.vs",
							":/core/glsl/particles/geometry/cube/cube.fs");
					_pickingShader = renderer->loadShaderProgram("particle_tristrip_cube_picking",
							":/core/glsl/particles/geometry/cube/picking/cube_tristrip.vs",
							":/core/glsl/particles/geometry/cube/picking/cube.fs");
				}
			}
		}
	}
	OVITO_ASSERT(_shader != nullptr);
	OVITO_ASSERT(_pickingShader != nullptr);

	// Prepare texture that is required for imposter rendering of spherical particles.
	if(shape == SphericalShape && shadingMode == NormalShading && (_renderingTechnique == POINT_SPRITES || _renderingTechnique == IMPOSTER_QUADS))
		initializeBillboardTexture(renderer);
}

/******************************************************************************
* Destructor.
******************************************************************************/
ViewportParticleGeometryBuffer::~ViewportParticleGeometryBuffer()
{
	destroyOpenGLResources();
}

/******************************************************************************
* Allocates a particle buffer with the given number of particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::setSize(int particleCount)
{
	OVITO_ASSERT(QOpenGLContextGroup::currentContextGroup() == _contextGroup);

	// Determine the required number of vertices that need to be sent to the graphics card per particle.
	int verticesPerParticle;
	if(_renderingTechnique == POINT_SPRITES) {
		verticesPerParticle = 1;
	}
	else if(_renderingTechnique == IMPOSTER_QUADS) {
		verticesPerParticle = 6;
	}
	else if(_renderingTechnique == CUBE_GEOMETRY) {
		if(_usingGeometryShader)
			verticesPerParticle = 1;
		else
			verticesPerParticle = 14;
	}
	else OVITO_ASSERT(false);

	_positionsBuffer.create(QOpenGLBuffer::StaticDraw, particleCount, verticesPerParticle);
	_radiiBuffer.create(QOpenGLBuffer::StaticDraw, particleCount, verticesPerParticle);
	_colorsBuffer.create(QOpenGLBuffer::StaticDraw, particleCount, verticesPerParticle);
}

/******************************************************************************
* Sets the coordinates of the particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::setParticlePositions(const Point3* coordinates)
{
	OVITO_ASSERT(QOpenGLContextGroup::currentContextGroup() == _contextGroup);
	_positionsBuffer.fill(coordinates);
}

/******************************************************************************
* Sets the radii of the particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::setParticleRadii(const FloatType* radii)
{
	OVITO_ASSERT(QOpenGLContextGroup::currentContextGroup() == _contextGroup);
	_radiiBuffer.fill(radii);
}

/******************************************************************************
* Sets the radius of all particles to the given value.
******************************************************************************/
void ViewportParticleGeometryBuffer::setParticleRadius(FloatType radius)
{
	OVITO_ASSERT(QOpenGLContextGroup::currentContextGroup() == _contextGroup);
	_radiiBuffer.fillConstant(radius);
}

/******************************************************************************
* Sets the colors of the particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::setParticleColors(const Color* colors)
{
	OVITO_ASSERT(QOpenGLContextGroup::currentContextGroup() == _contextGroup);
	_colorsBuffer.fill(colors);
}

/******************************************************************************
* Sets the color of all particles to the given value.
******************************************************************************/
void ViewportParticleGeometryBuffer::setParticleColor(const Color color)
{
	OVITO_ASSERT(QOpenGLContextGroup::currentContextGroup() == _contextGroup);
	_colorsBuffer.fillConstant(color);
}

/******************************************************************************
* Returns true if the geometry buffer is filled and can be rendered with the given renderer.
******************************************************************************/
bool ViewportParticleGeometryBuffer::isValid(SceneRenderer* renderer)
{
	ViewportSceneRenderer* vpRenderer = dynamic_object_cast<ViewportSceneRenderer>(renderer);
	if(!vpRenderer) return false;
	return _positionsBuffer.isCreated() && (_contextGroup == vpRenderer->glcontext()->shareGroup());
}

/******************************************************************************
* Renders the geometry.
******************************************************************************/
void ViewportParticleGeometryBuffer::render(SceneRenderer* renderer)
{
	OVITO_CHECK_OPENGL();
	OVITO_ASSERT(_contextGroup == QOpenGLContextGroup::currentContextGroup());
	OVITO_STATIC_ASSERT(sizeof(FloatType) == 4);
	OVITO_STATIC_ASSERT(sizeof(Color) == 12);
	OVITO_STATIC_ASSERT(sizeof(Point3) == 12);

	ViewportSceneRenderer* vpRenderer = dynamic_object_cast<ViewportSceneRenderer>(renderer);

	if(particleCount() <= 0 || !vpRenderer)
		return;

	if(_renderingTechnique == POINT_SPRITES)
		renderPointSprites(vpRenderer);
	else if(_renderingTechnique == IMPOSTER_QUADS)
		renderImposters(vpRenderer);
	else if(_renderingTechnique == CUBE_GEOMETRY)
		renderCubes(vpRenderer);
}

/******************************************************************************
* Renders the particles using OpenGL point sprites.
******************************************************************************/
void ViewportParticleGeometryBuffer::renderPointSprites(ViewportSceneRenderer* renderer)
{
	OVITO_ASSERT(_positionsBuffer.verticesPerElement() == 1);

	// Let the vertex shader compute the point size.
	OVITO_CHECK_OPENGL(glEnable(GL_VERTEX_PROGRAM_POINT_SIZE));

	// Enable point sprites when using the compatibility OpenGL profile.
	// In the core profile, they are already enabled by default.
	if(renderer->isCoreProfile() == false) {
		OVITO_CHECK_OPENGL(glEnable(GL_POINT_SPRITE));

		// Specify point sprite texture coordinate replacement mode.
		glTexEnvf(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
	}

	if(particleShape() == SphericalShape && shadingMode() == NormalShading && !renderer->isPicking())
		activateBillboardTexture(renderer);

	// Pick the right OpenGL shader program.
	QOpenGLShaderProgram* shader = renderer->isPicking() ? _pickingShader : _shader;
	if(!shader->bind())
		throw Exception(QStringLiteral("Failed to bind OpenGL shader program."));

	// This is how our point sprite's size will be modified based on the distance from the viewer.
	GLint viewportCoords[4];
	glGetIntegerv(GL_VIEWPORT, viewportCoords);
	float param = renderer->projParams().projectionMatrix(1,1) * viewportCoords[3];

	if(renderer->isCoreProfile() == false) {
		// This is a fallback if GL_VERTEX_PROGRAM_POINT_SIZE is not supported.
		std::array<float,3> distanceAttenuation;
		if(renderer->projParams().isPerspective)
			distanceAttenuation = std::array<float,3>{{ 0.0f, 0.0f, 1.0f / (param * param) }};
		else
			distanceAttenuation = std::array<float,3>{{ 1.0f / param, 0.0f, 0.0f }};
		OVITO_CHECK_OPENGL(glPointSize(1));
		OVITO_CHECK_OPENGL(renderer->glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, distanceAttenuation.data()));
	}

	shader->setUniformValue("basePointSize", param);
	shader->setUniformValue("projection_matrix", (QMatrix4x4)renderer->projParams().projectionMatrix);
	shader->setUniformValue("modelview_matrix", (QMatrix4x4)renderer->modelViewTM());

	_positionsBuffer.bindPositions(renderer, shader);
	_radiiBuffer.bind(renderer, shader, "particle_radius", GL_FLOAT, 0, 1);
	if(!renderer->isPicking()) {
		_colorsBuffer.bindColors(renderer, shader, 3);
	}
	else {
		OVITO_CHECK_OPENGL(shader->setUniformValue("pickingBaseID", (GLint)renderer->registerSubObjectIDs(particleCount())));
		renderer->activateVertexIDs(shader, particleCount());
	}

	OVITO_CHECK_OPENGL(glDrawArrays(GL_POINTS, 0, particleCount()));

	OVITO_CHECK_OPENGL(glDisable(GL_VERTEX_PROGRAM_POINT_SIZE));
	_positionsBuffer.detachPositions(renderer, shader);
	_radiiBuffer.detach(renderer, shader, "particle_radius");
	if(!renderer->isPicking())
		_colorsBuffer.detachColors(renderer, shader);
	else
		renderer->deactivateVertexIDs(shader);
	shader->release();

	// Disable point sprites again.
	if(renderer->isCoreProfile() == false)
		OVITO_CHECK_OPENGL(glDisable(GL_POINT_SPRITE));

	if(particleShape() == SphericalShape && shadingMode() == NormalShading && !renderer->isPicking())
		deactivateBillboardTexture(renderer);
}

/******************************************************************************
* Renders a cube for each particle using triangle strips.
******************************************************************************/
void ViewportParticleGeometryBuffer::renderCubes(ViewportSceneRenderer* renderer)
{
	OVITO_ASSERT(!_usingGeometryShader || _positionsBuffer.verticesPerElement() == 1);
	OVITO_ASSERT(_usingGeometryShader || _positionsBuffer.verticesPerElement() == 14);

	// Pick the right OpenGL shader program.
	QOpenGLShaderProgram* shader = renderer->isPicking() ? _pickingShader : _shader;
	if(!shader->bind())
		throw Exception(QStringLiteral("Failed to bind OpenGL shader program."));

	// Need to render only the front facing sides of the cubes.
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	// This is to draw the cube with a single triangle strip.
	// The cube vertices:
	static const GLfloat cubeVerts[14][3] = {
		{ 1,  1,  1},
		{ 1, -1,  1},
		{ 1,  1, -1},
		{ 1, -1, -1},
		{-1, -1, -1},
		{ 1, -1,  1},
		{-1, -1,  1},
		{ 1,  1,  1},
		{-1,  1,  1},
		{ 1,  1, -1},
		{-1,  1, -1},
		{-1, -1, -1},
		{-1,  1,  1},
		{-1, -1,  1},
	};
	shader->setUniformValueArray("cubeVerts", &cubeVerts[0][0], 14, 3);

	// Set up look-up table for triangle strips.
	if(particleShape() != SphericalShape && !renderer->isPicking()) {
		// The normal vectors for the cube triangle strip.
		static const GLfloat normals[14][3] = {
			{ 1,  0,  0},
			{ 1,  0,  0},
			{ 1,  0,  0},
			{ 1,  0,  0},
			{ 0,  0, -1},
			{ 0, -1,  0},
			{ 0, -1,  0},
			{ 0,  0,  1},
			{ 0,  0,  1},
			{ 0,  1,  0},
			{ 0,  1,  0},
			{ 0,  0, -1},
			{-1,  0,  0},
			{-1,  0,  0}
		};
		shader->setUniformValueArray("normals", &normals[0][0], 14, 3);
		shader->setUniformValue("normal_matrix", (QMatrix3x3)(renderer->modelViewTM().linear().inverse().transposed()));
	}

	shader->setUniformValue("projection_matrix", (QMatrix4x4)renderer->projParams().projectionMatrix);
	shader->setUniformValue("inverse_projection_matrix", (QMatrix4x4)renderer->projParams().inverseProjectionMatrix);
	shader->setUniformValue("modelview_matrix", (QMatrix4x4)renderer->modelViewTM());
	shader->setUniformValue("modelviewprojection_matrix", (QMatrix4x4)(renderer->projParams().projectionMatrix * renderer->modelViewTM()));
	shader->setUniformValue("is_perspective", renderer->projParams().isPerspective);

	GLint viewportCoords[4];
	glGetIntegerv(GL_VIEWPORT, viewportCoords);
	shader->setUniformValue("viewport_origin", (float)viewportCoords[0], (float)viewportCoords[1]);
	shader->setUniformValue("inverse_viewport_size", 2.0f / (float)viewportCoords[2], 2.0f / (float)viewportCoords[3]);

	_positionsBuffer.bindPositions(renderer, shader);
	_radiiBuffer.bind(renderer, shader, "particle_radius", GL_FLOAT, 0, 1);
	if(!renderer->isPicking()) {
		_colorsBuffer.bindColors(renderer, shader, 3);
	}
	else {
		OVITO_CHECK_OPENGL(shader->setUniformValue("pickingBaseID", (GLint)renderer->registerSubObjectIDs(particleCount())));
		renderer->activateVertexIDs(shader, particleCount() * _positionsBuffer.verticesPerElement());
	}

	if(_usingGeometryShader) {
		OVITO_CHECK_OPENGL(glDrawArrays(GL_POINTS, 0, particleCount()));
	}
	else {
		// Prepare arrays required for glMultiDrawArrays().
		if(_primitiveStartIndices.size() != particleCount()) {
			_primitiveStartIndices.resize(particleCount());
			_primitiveVertexCounts.resize(particleCount());
			GLint index = 0;
			for(GLint& s : _primitiveStartIndices) {
				s = index;
				index += _positionsBuffer.verticesPerElement();
			}
			std::fill(_primitiveVertexCounts.begin(), _primitiveVertexCounts.end(), _positionsBuffer.verticesPerElement());
		}

		renderer->activateVertexIDs(shader, particleCount() * _positionsBuffer.verticesPerElement(), renderer->isPicking());

		OVITO_CHECK_OPENGL(renderer->glMultiDrawArrays(GL_TRIANGLE_STRIP,
				_primitiveStartIndices.data(),
				_primitiveVertexCounts.data(),
				_primitiveStartIndices.size()));

		renderer->deactivateVertexIDs(shader, renderer->isPicking());
	}

	_positionsBuffer.detachPositions(renderer, shader);
	_radiiBuffer.detach(renderer, shader, "particle_radius");
	if(!renderer->isPicking())
		_colorsBuffer.detachColors(renderer, shader);
	else
		renderer->deactivateVertexIDs(shader);

	shader->release();
}

/******************************************************************************
* Renders particles using quads.
******************************************************************************/
void ViewportParticleGeometryBuffer::renderImposters(ViewportSceneRenderer* renderer)
{
	OVITO_ASSERT(_positionsBuffer.verticesPerElement() == 6);

	// Pick the right OpenGL shader program.
	QOpenGLShaderProgram* shader = renderer->isPicking() ? _pickingShader : _shader;
	if(!shader->bind())
		throw Exception(QStringLiteral("Failed to bind OpenGL shader program."));

	if(particleShape() == SphericalShape && shadingMode() == NormalShading && !renderer->isPicking())
		activateBillboardTexture(renderer);

	// Need to render only the front facing sides.
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	// The texture coordinates of a quad made of two triangles.
	static const QVector2D texcoords[6] = {{0,1},{1,1},{1,0},{0,1},{1,0},{0,0}};
	shader->setUniformValueArray("imposter_texcoords", &texcoords[0], 6);

	// The coordinate offsets of the six vertices of a quad made of two triangles.
	static const QVector4D voffsets[6] = {{-1,-1,0,0},{1,-1,0,0},{1,1,0,0},{-1,-1,0,0},{1,1,0,0},{-1,1,0,0}};
	shader->setUniformValueArray("imposter_voffsets", &voffsets[0], 6);

	shader->setUniformValue("projection_matrix", (QMatrix4x4)renderer->projParams().projectionMatrix);
	shader->setUniformValue("modelview_matrix", (QMatrix4x4)renderer->modelViewTM());

	_positionsBuffer.bindPositions(renderer, shader);
	_radiiBuffer.bind(renderer, shader, "particle_radius", GL_FLOAT, 0, 1);
	if(!renderer->isPicking()) {
		_colorsBuffer.bindColors(renderer, shader, 3);
	}
	else {
		OVITO_CHECK_OPENGL(shader->setUniformValue("pickingBaseID", (GLint)renderer->registerSubObjectIDs(particleCount())));
		renderer->activateVertexIDs(shader, particleCount() * _positionsBuffer.verticesPerElement());
	}

	renderer->activateVertexIDs(shader, particleCount() * _positionsBuffer.verticesPerElement());

	OVITO_CHECK_OPENGL(glDrawArrays(GL_TRIANGLES, 0, particleCount() * _positionsBuffer.verticesPerElement()));

	renderer->deactivateVertexIDs(shader);

	_positionsBuffer.detachPositions(renderer, shader);
	_radiiBuffer.detach(renderer, shader, "particle_radius");
	if(!renderer->isPicking())
		_colorsBuffer.detachColors(renderer, shader);
	else
		renderer->deactivateVertexIDs(shader);
	shader->release();

	if(particleShape() == SphericalShape && shadingMode() == NormalShading && !renderer->isPicking())
		deactivateBillboardTexture(renderer);
}

/******************************************************************************
* Creates the textures used for billboard rendering of particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::initializeBillboardTexture(ViewportSceneRenderer* renderer)
{
	static std::vector<std::array<GLubyte,4>> textureImages[BILLBOARD_TEXTURE_LEVELS];
	static bool generatedImages = false;

	if(generatedImages == false) {
		generatedImages = true;
		for(int mipmapLevel = 0; mipmapLevel < BILLBOARD_TEXTURE_LEVELS; mipmapLevel++) {
			int resolution = (1 << (BILLBOARD_TEXTURE_LEVELS - mipmapLevel - 1));
			textureImages[mipmapLevel].resize(resolution*resolution);
			size_t pixelOffset = 0;
			for(int y = 0; y < resolution; y++) {
				for(int x = 0; x < resolution; x++, pixelOffset++) {
					Vector2 r((FloatType(x - resolution/2) + 0.5) / (resolution/2), (FloatType(y - resolution/2) + 0.5) / (resolution/2));
					FloatType r2 = r.squaredLength();
					FloatType r2_clamped = std::min(r2, FloatType(1));
					FloatType diffuse_brightness = sqrt(1 - r2_clamped) * 0.6 + 0.4;

					textureImages[mipmapLevel][pixelOffset][0] =
							(GLubyte)(std::min(diffuse_brightness, (FloatType)1.0) * 255.0);

					textureImages[mipmapLevel][pixelOffset][2] = 255;
					textureImages[mipmapLevel][pixelOffset][3] = 255;

					if(r2 < 1.0) {
						// Store specular brightness in alpha channel of texture.
						Vector2 sr = r + Vector2(0.6883, 0.982);
						FloatType specular = std::max(FloatType(1) - sr.squaredLength(), FloatType(0));
						specular *= specular;
						specular *= specular * (1 - r2_clamped*r2_clamped);
						textureImages[mipmapLevel][pixelOffset][1] =
								(GLubyte)(std::min(specular, FloatType(1)) * 255.0);
					}
					else {
						// Set transparent pixel.
						textureImages[mipmapLevel][pixelOffset][1] = 0;
					}
				}
			}
		}
	}

	renderer->glfuncs()->glActiveTexture(GL_TEXTURE0);

	// Create OpenGL texture.
	glGenTextures(1, &_billboardTexture);

	// Make sure texture gets deleted again when this object is destroyed.
	attachOpenGLResources();

	// Transfer pixel data to OpenGL texture.
	OVITO_CHECK_OPENGL(glBindTexture(GL_TEXTURE_2D, _billboardTexture));
	for(int mipmapLevel = 0; mipmapLevel < BILLBOARD_TEXTURE_LEVELS; mipmapLevel++) {
		int resolution = (1 << (BILLBOARD_TEXTURE_LEVELS - mipmapLevel - 1));

		OVITO_CHECK_OPENGL(glTexImage2D(GL_TEXTURE_2D, mipmapLevel, GL_RGBA,
				resolution, resolution, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureImages[mipmapLevel].data()));
	}
}

/******************************************************************************
* This method that takes care of freeing the shared OpenGL resources owned
* by this class.
******************************************************************************/
void ViewportParticleGeometryBuffer::freeOpenGLResources()
{
	glDeleteTextures(1, &_billboardTexture);
	_billboardTexture = 0;
}

/******************************************************************************
* Activates a texture for billboard rendering of spherical particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::activateBillboardTexture(ViewportSceneRenderer* renderer)
{
	OVITO_ASSERT(_billboardTexture != 0);
	OVITO_ASSERT(shadingMode() != FlatShading);
	OVITO_ASSERT(!renderer->isPicking());
	OVITO_ASSERT(particleShape() == SphericalShape);

	// Enable texture mapping when using compatibility OpenGL.
	// In the core profile, this is already enabled by default.
	if(renderer->isCoreProfile() == false)
		OVITO_CHECK_OPENGL(glEnable(GL_TEXTURE_2D));

	OVITO_CHECK_OPENGL(renderer->glfuncs()->glActiveTexture(GL_TEXTURE0));
	OVITO_CHECK_OPENGL(glBindTexture(GL_TEXTURE_2D, _billboardTexture));

	OVITO_CHECK_OPENGL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST));
	OVITO_CHECK_OPENGL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));

	OVITO_STATIC_ASSERT(BILLBOARD_TEXTURE_LEVELS >= 3);
	OVITO_CHECK_OPENGL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, BILLBOARD_TEXTURE_LEVELS - 3));
}

/******************************************************************************
* Deactivates the texture used for billboard rendering of spherical particles.
******************************************************************************/
void ViewportParticleGeometryBuffer::deactivateBillboardTexture(ViewportSceneRenderer* renderer)
{
	// Disable texture mapping again when not using core profile.
	if(renderer->isCoreProfile() == false)
		OVITO_CHECK_OPENGL(glDisable(GL_TEXTURE_2D));
}

};
