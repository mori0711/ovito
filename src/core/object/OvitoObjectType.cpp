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
#include "OvitoObjectType.h"
#include "NativeOvitoObjectType.h"
#include "OvitoObject.h"

#include <core/plugins/Plugin.h>
#include <core/plugins/PluginManager.h>
#include <core/io/ObjectSaveStream.h>
#include <core/io/ObjectLoadStream.h>

#if 0
#include <core/reference/PropertyFieldDescriptor.h>
#include <core/reference/RefTarget.h>
#endif

namespace Ovito {

/// The descriptor object for the root class of Ovito's object system.
/// This class is named "OvitoObject".
const OvitoObjectType& OvitoObjectType::_rootClass = OvitoObject::OOType;

/******************************************************************************
* Constructor of the object.
******************************************************************************/
OvitoObjectType::OvitoObjectType(const QString& name, const OvitoObjectType* superClass, bool isAbstract, bool isSerializable) :
	_name(name), _plugin(nullptr), _isAbstract(isAbstract), _superClass(superClass),
	_isSerializable(isSerializable), _firstChild(NULL), _firstPropertyField(NULL)
{
	OVITO_ASSERT(superClass != NULL || name == "OvitoObject");

	// Insert this object type into the global list of classes.
	_next = _rootClass._next;
	const_cast<OvitoObjectType&>(_rootClass)._next = this;

	if(superClass) {
		// Insert into linked list of base class.
		_nextSibling = superClass->_firstChild;
		const_cast<OvitoObjectType*>(superClass)->_firstChild = this;

		// Inherit serializable attribute.
		if(!superClass->isSerializable())
			_isSerializable = false;
	}
}

/******************************************************************************
* Creates an object of the appropriate kind.
* Throws an exception if the containing plugin failed to load.
******************************************************************************/
OORef<OvitoObject> OvitoObjectType::createInstance()
{
	OVITO_CHECK_POINTER(plugin());

	if(!plugin()->isLoaded()) {
		// Load plugin first.
		try {
			plugin()->loadPlugin();
		}
		catch(Exception& ex) {
			throw ex.prependGeneralMessage(Plugin::tr("Could not create instance of class %1. Failed to load plugin '%2'").arg(name()).arg(plugin()->pluginId()));
		}
	}
	if(isAbstract())
		throw Exception(Plugin::tr("Cannot instantiate abstract class '%1'.").arg(name()));

	return createInstanceImpl();
}

/******************************************************************************
* Writes a class descriptor to the stream. This is for internal use of the core only.
******************************************************************************/
void OvitoObjectType::serializeRTTI(ObjectSaveStream& stream, const OvitoObjectType* type)
{
	OVITO_CHECK_POINTER(type);

	stream.beginChunk(0x10000000);
	stream << type->plugin()->pluginId();
	stream << type->name();
	stream.endChunk();
}

/******************************************************************************
* Loads a class descriptor from the stream. This is for internal use of the core only.
* Throws an exception if the class is not defined or the required plugin is not installed.
******************************************************************************/
OvitoObjectType* OvitoObjectType::deserializeRTTI(ObjectLoadStream& stream)
{
	QString pluginId, className;
	stream.expectChunk(0x10000000);
	stream >> pluginId;
	stream >> className;
	stream.closeChunk();

	// Lookup class descriptor.
	Plugin* plugin = PluginManager::instance().plugin(pluginId);
	if(plugin == NULL)
		throw Exception(Plugin::tr("A required plugin is not installed: %1").arg(pluginId));
	OVITO_CHECK_POINTER(plugin);
	OvitoObjectType* type = plugin->findClass(className);
	if(type == NULL)
		throw Exception(Plugin::tr("Required class %1 not found in plugin %2.").arg(className, pluginId));

	return type;
}

/******************************************************************************
* Searches for a property field defined in this class.
******************************************************************************/
const PropertyFieldDescriptor* OvitoObjectType::findPropertyField(const char* identifier) const
{
#if 0
	for(const PropertyFieldDescriptor* field = firstPropertyField(); field; field = field->next())
		if(qstrcmp(field->identifier(), identifier) == 0) return field;
#endif
	return NULL;
}

};
