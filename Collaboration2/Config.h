/***********************************************************************
Config - Configuration header for second-generation collaboration
infrastructure.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef CONFIG_INCLUDED
#define CONFIG_INCLUDED

#define COLLABORATION_PLUGINDIR_DEBUG "/opt/Vrui-6.0/lib64/debug/CollaborationPlugins-4.0"
#define COLLABORATION_PLUGINDIR_RELEASE "/opt/Vrui-6.0/lib64/CollaborationPlugins-4.0"
#ifdef DEBUG
	#define COLLABORATION_PLUGINDIR COLLABORATION_PLUGINDIR_DEBUG
#else
	#define COLLABORATION_PLUGINDIR COLLABORATION_PLUGINDIR_RELEASE
#endif

#define COLLABORATION_PLUGINSERVERDSONAMETEMPLATE "lib%s.%u-Server.so"
#define COLLABORATION_PLUGINCLIENTDSONAMETEMPLATE "lib%s.%u-Client.so"

#define COLLABORATION_CONFIGDIR "/opt/Vrui-6.0/etc/Collaboration-4.0"
#define COLLABORATION_CONFIGFILENAME "Collaboration.cfg"

#define COLLABORATION_RESOURCEDIR "/opt/Vrui-6.0/share/Collaboration-4.0"

#define COLLABORATION_HAVE_GETENTROPY 1

#endif
