########################################################################
# Makefile for Vrui collaboration infrastructure.
# Copyright (c) 2009-2020 Oliver Kreylos
#
# This file is part of the WhyTools Build Environment.
# 
# The WhyTools Build Environment is free software; you can redistribute
# it and/or modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
# 
# The WhyTools Build Environment is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with the WhyTools Build Environment; if not, write to the Free
# Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
# 02111-1307 USA
########################################################################

# Directory containing the Vrui build system. The directory below
# matches the default Vrui installation; if Vrui's installation
# directory was changed during Vrui's installation, the directory below
# must be adapted.
VRUI_MAKEDIR := /usr/local/share/Vrui-6.0/make
ifdef DEBUG
  VRUI_MAKEDIR := $(VRUI_MAKEDIR)/debug
endif

# Uncomment the following line to receive status messages from the
# protocol engine:
# CFLAGS += -DVERBOSE

#
# Check if the system has the getentropy function call. If this fails,
# override by setting SYSTEM_HAVE_GETENTROPY = 0
#

ifneq ($(wildcard /usr/include/sys/random.h),)
  SYSTEM_HAVE_GETENTROPY = 1
else
  SYSTEM_HAVE_GETENTROPY = 0
endif

########################################################################
# Everything below here should not have to be changed
########################################################################

# Define the root of the toolkit source tree
PACKAGEROOT := $(shell pwd)

# Specify version of created dynamic shared libraries
COLLABORATION_VERSION = 4000
MAJORLIBVERSION = 4
MINORLIBVERSION = 0
COLLABORATION_NAME := Collaboration-$(MAJORLIBVERSION).$(MINORLIBVERSION)

# Root directory for Collaboration plug-ins underneath Vrui's library
# directory:
COLLABORATIONPLUGINDIREXT = CollaborationPlugins-$(MAJORLIBVERSION).$(MINORLIBVERSION)

# Root directory for Collaboration configuration data underneath Vrui's
# configuration directory:
COLLABORATIONCONFIGDIREXT = $(COLLABORATION_NAME)

# Root directory for Collaboration resource data underneath Vrui's
# shared data directory:
COLLABORATIONRESOURCEDIREXT = $(COLLABORATION_NAME)

# Include definitions for the system environment and system-provided
# packages
include $(VRUI_MAKEDIR)/SystemDefinitions
include $(VRUI_MAKEDIR)/Packages.System
include $(VRUI_MAKEDIR)/Configuration.Vrui
include $(VRUI_MAKEDIR)/Packages.Vrui
include $(PACKAGEROOT)/BuildRoot/Packages.Collaboration

# Set destination directories for libraries and plug-ins:
LIBDESTDIR := $(PACKAGEROOT)/$(MYLIBEXT)
VISLETDESTDIR := $(PACKAGEROOT)/$(PLUGINDIR)/Vislets
PLUGINDESTDIR := $(PACKAGEROOT)/$(PLUGINDIR)/Plugins

# Set installation directories:
MYPLUGININSTALLDIR_DEBUG = $(PLUGININSTALLDIR_DEBUG)/$(COLLABORATIONPLUGINDIREXT)
MYPLUGININSTALLDIR_RELEASE = $(PLUGININSTALLDIR_RELEASE)/$(COLLABORATIONPLUGINDIREXT)
#ifdef DEBUG
  MYPLUGININSTALLDIR = $(MYPLUGININSTALLDIR_DEBUG)
#else
  MYPLUGININSTALLDIR = $(MYPLUGININSTALLDIR_RELEASE)
#endif
MYETCINSTALLDIR = $(ETCINSTALLDIR)/$(COLLABORATIONCONFIGDIREXT)
MYSHAREINSTALLDIR = $(SHAREINSTALLDIR)/$(COLLABORATIONRESOURCEDIREXT)

########################################################################
# Specify additional compiler and linker flags
########################################################################

CFLAGS += -Wall -pedantic

# Override the include file and library search directories:
EXTRACINCLUDEFLAGS += -I$(PACKAGEROOT)
EXTRALINKDIRFLAGS += -L$(LIBDESTDIR)

########################################################################
# List packages used by this project
# (Supported packages can be found in
# $(VRUI_MAKEDIR)/BuildRoot/Packages)
########################################################################

PACKAGES = 

########################################################################
# Specify all final targets
########################################################################

LIBRARIES = 
PLUGINS = 
VISLETS = 
EXECUTABLES = 

LIBRARY_NAMES = libCollaboration2Server \
                libCollaboration2Client

LIBRARIES += $(LIBRARY_NAMES:%=$(call LIBRARYNAME,%))

#
# The protocol plug-ins:
#

CHAT_VERSION = 1
KOINONIA_VERSION = 1
AGORA_VERSION = 1
VRUICORE_VERSION = 1
VRUIAGORA_VERSION = 1
ENSOMATOSIS_VERSION = 1

PLUGIN_NAMES = Chat.$(CHAT_VERSION) \
               Koinonia.$(KOINONIA_VERSION) \
               Agora.$(AGORA_VERSION) \
               VruiCore.$(VRUICORE_VERSION) \
               VruiAgora.$(VRUIAGORA_VERSION) \
               Ensomatosis.$(ENSOMATOSIS_VERSION)

PLUGINNAME = $(PLUGINDESTDIR)/lib$(1).$(PLUGINFILEEXT)
PLUGIN_SERVERS = $(PLUGIN_NAMES:%=$(call PLUGINNAME,%-Server))
PLUGIN_CLIENTS = $(PLUGIN_NAMES:%=$(call PLUGINNAME,%-Client))

# Function to generate plug-in object file names:
MYPLUGINOBJNAMES = $(1:%.cpp=$(OBJDIR)/pic/Collaboration2/Plugins/%.o)

PLUGINS += $(PLUGIN_SERVERS) \
           $(PLUGIN_CLIENTS)

#
# The vislet plug-ins:
#

VISLET_NAMES = Collaboration2

VISLETNAME = $(VISLETDESTDIR)/lib$(1).$(PLUGINFILEEXT)
VISLETS += $(VISLET_NAMES:%=$(call VISLETNAME,%))

#
# Executables:
#

# The main collaboration server:
EXECUTABLES += $(EXEDIR)/Server2

# The collaboration client test program:
EXECUTABLES += $(EXEDIR)/VruiCoreTest

#
# Other targets:
#

# The make configuration file:
ifdef DEBUG
	MAKECONFIGFILE = BuildRoot/Configuration.Collaboration.debug
else
	MAKECONFIGFILE = BuildRoot/Configuration.Collaboration
endif

#
# Rule to built all targets:
#

ALL = $(LIBRARIES) $(EXECUTABLES) $(PLUGINS) $(VISLETS) $(MAKECONFIGFILE)

.PHONY: all
all: $(ALL)

########################################################################
# Pseudo-target to print configuration options and configure libraries
########################################################################

.PHONY: config config-invalidate
config: config-invalidate $(DEPDIR)/config

config-invalidate:
	@mkdir -p $(DEPDIR)
	@touch $(DEPDIR)/Configure-Begin

$(DEPDIR)/Configure-Begin:
	@mkdir -p $(DEPDIR)
	@echo "---- Vrui Collaboration Infrastructure configuration options: ----"
ifneq ($(SYSTEM_HAVE_OPUS),0)
	@echo "Audio transmission in Agora plug-in enabled"
else
	@echo "Audio transmission in Agora plug-in disabled"
endif
	@touch $(DEPDIR)/Configure-Begin

$(DEPDIR)/Configure-Collaboration: $(DEPDIR)/Configure-Begin
	@cp Collaboration2/Config.h Collaboration2/Config.h.temp
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_PLUGINDIR_DEBUG,$(MYPLUGININSTALLDIR_DEBUG))
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_PLUGINDIR_RELEASE,$(MYPLUGININSTALLDIR_RELEASE))
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_PLUGINSERVERDSONAMETEMPLATE,lib%s.%u-Server.$(PLUGINFILEEXT))
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_PLUGINCLIENTDSONAMETEMPLATE,lib%s.%u-Client.$(PLUGINFILEEXT))
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_CONFIGDIR,$(MYETCINSTALLDIR))
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_CONFIGDIR,$(MYETCINSTALLDIR))
	@$(call CONFIG_SETSTRINGVAR,Collaboration2/Config.h.temp,COLLABORATION_RESOURCEDIR,$(MYSHAREINSTALLDIR))
	@$(call CONFIG_SETVAR,Collaboration2/Config.h.temp,COLLABORATION_HAVE_GETENTROPY,$(SYSTEM_HAVE_GETENTROPY))
	@if ! diff Collaboration2/Config.h.temp Collaboration2/Config.h > /dev/null ; then cp Collaboration2/Config.h.temp Collaboration2/Config.h ; fi
	@rm Collaboration2/Config.h.temp
	@touch $(DEPDIR)/Configure-Collaboration

$(DEPDIR)/Configure-Install: $(DEPDIR)/Configure-Collaboration
	@echo "---- Vrui Collaboration Infrastructure installation configuration ----"
	@echo "Installation directory: $(VRUI_PACKAGEROOT)"
	@echo "Protocol plug-in directory: $(MYPLUGININSTALLDIR)"
	@echo "Vislet plug-in directory: $(PLUGININSTALLDIR)/$(VRVISLETSDIREXT)"
	@echo "Configuration directory: $(MYETCINSTALLDIR)"
	@echo "Resource directory: $(MYSHAREINSTALLDIR)"
	@touch $(DEPDIR)/Configure-Install

$(DEPDIR)/Configure-End: $(DEPDIR)/Configure-Install
	@echo "---- End of Vrui Collaboration Infrastructure configuration options ----"
	@touch $(DEPDIR)/Configure-End

$(DEPDIR)/config: $(DEPDIR)/Configure-End
	@touch $(DEPDIR)/config

########################################################################
# Specify other actions to be performed on a `make clean'
########################################################################

.PHONY: extraclean
extraclean:
	-rm -f $(LIBRARY_NAMES:%=$(LIBDESTDIR)/$(call DSONAME,%))
	-rm -f $(LIBRARY_NAMES:%=$(LIBDESTDIR)/$(call LINKDSONAME,%))

.PHONY: extrasqueakyclean
extrasqueakyclean:
	-rm -f $(ALL)
	-rm -rf $(PACKAGEROOT)/$(LIBEXT)

# Include basic makefile
include $(VRUI_MAKEDIR)/BasicMakefile

########################################################################
# Specify build rules for dynamic shared objects and executables
########################################################################

# Make all plug-ins depend on configuration:
$(call PLUGINOBJNAMES,$(wildcard Collaboration2/Plugins/*.cpp)): | $(DEPDIR)/config

# Sources common to the server- and client-side libraries:
COMMON_SOURCES = Collaboration2/Allocator.cpp \
                 Collaboration2/NonBlockSocket.cpp \
                 Collaboration2/UDPSocket.cpp \
                 Collaboration2/DataType.cpp

#
# Server-side library, plug-ins, and executables:
#

# Sources for the server-side library:
SERVER_SOURCES = $(COMMON_SOURCES) \
                 Collaboration2/PluginServer.cpp \
                 Collaboration2/Server.cpp

$(call LIBOBJNAMES,$(SERVER_SOURCES)): | $(DEPDIR)/config

# The server-side library:
$(call LIBRARYNAME,libCollaboration2Server): PACKAGES = $(MYCOLLABORATION2SERVER_DEPENDS)
$(call LIBRARYNAME,libCollaboration2Server): $(call LIBOBJNAMES,$(SERVER_SOURCES))
.PHONY: libCollaboration2Server
libCollaboration2Server: $(call LIBRARYNAME,libCollaboration2Server)

# Make all server components depend on collaboration server library:
$(PLUGIN_SERVERS) $(EXEDIR)/Server2: | $(call LIBRARYNAME,libCollaboration2Server)

# Implicit rule to link server-side plug-ins:
$(call PLUGINNAME,%-Server): PACKAGES += MYCOLLABORATION2SERVER
$(call PLUGINNAME,%-Server): HOSTPACKAGES += MYCOLLABORATION2SERVER
$(call PLUGINNAME,%-Server):
	@mkdir -p $(PLUGINDESTDIR)
ifdef SHOWCOMMAND
	$(CCOMP) $(PLUGINLINKFLAGS) -o $@ $^ $(PLUGINLFLAGS)
else
	@echo Linking $@...
	@$(CCOMP) $(PLUGINLINKFLAGS) -o $@ $^ $(PLUGINLFLAGS)
endif

# The simple text chat protocol:
$(call PLUGINNAME,Chat.$(CHAT_VERSION)-Server): PACKAGES = MYMISC
$(call PLUGINNAME,Chat.$(CHAT_VERSION)-Server): $(call MYPLUGINOBJNAMES,ChatProtocol.cpp ChatServer.cpp)

# The data object sharing protocol:
$(call PLUGINNAME,Koinonia.$(KOINONIA_VERSION)-Server): PACKAGES = MYMISC
$(call PLUGINNAME,Koinonia.$(KOINONIA_VERSION)-Server): $(call MYPLUGINOBJNAMES,KoinoniaProtocol.cpp KoinoniaServer.cpp)

# The simple group audio protocol:
$(call PLUGINNAME,Agora.$(AGORA_VERSION)-Server): PACKAGES = MYMISC
$(call PLUGINNAME,Agora.$(AGORA_VERSION)-Server): $(call MYPLUGINOBJNAMES,AgoraProtocol.cpp AgoraServer.cpp)

# Packages used by the VruiCore server plug-in:
VRUICORESERVER_PACKAGES = MYGEOMETRY MYMISC

# The Vrui Core protocol:
$(call PLUGINNAME,VruiCore.$(VRUICORE_VERSION)-Server): PACKAGES = $(VRUICORESERVER_PACKAGES)
$(call PLUGINNAME,VruiCore.$(VRUICORE_VERSION)-Server): $(call MYPLUGINOBJNAMES,VruiCoreProtocol.cpp VruiCoreServer.cpp)

# The Vrui-embedded group audio protocol:
$(call PLUGINNAME,VruiAgora.$(VRUIAGORA_VERSION)-Server): PACKAGES = MYMISC $(VRUICORESERVER_PACKAGES)
$(call PLUGINNAME,VruiAgora.$(VRUIAGORA_VERSION)-Server): HOSTPACKAGES = $(VRUICORESERVER_PACKAGES)
$(call PLUGINNAME,VruiAgora.$(VRUIAGORA_VERSION)-Server): $(call MYPLUGINOBJNAMES,VruiAgoraProtocol.cpp VruiAgoraServer.cpp)

# The IK avatar protocol:
$(call PLUGINNAME,Ensomatosis.$(ENSOMATOSIS_VERSION)-Server): PACKAGES = MYVRUI MYGEOMETRY MYMISC $(VRUICORESERVER_PACKAGES)
$(call PLUGINNAME,Ensomatosis.$(ENSOMATOSIS_VERSION)-Server): HOSTPACKAGES = $(VRUICORESERVER_PACKAGES)
$(call PLUGINNAME,Ensomatosis.$(ENSOMATOSIS_VERSION)-Server): $(call MYPLUGINOBJNAMES,EnsomatosisProtocol.cpp EnsomatosisServer.cpp)

# The main collaboration server:
$(OBJDIR)/Server2.o: | $(DEPDIR)/config
$(EXEDIR)/Server2: PACKAGES = MYCOLLABORATION2SERVER
$(EXEDIR)/Server2: LINKFLAGS += $(PLUGINHOSTLINKFLAGS)
$(EXEDIR)/Server2: $(OBJDIR)/Server2.o
.PHONY: Server2
Server2: $(EXEDIR)/Server2

#
# Client-side library, plug-ins, vislets, and executables:
#

# Sources for the client-side library:
CLIENT_SOURCES = $(COMMON_SOURCES) \
                 Collaboration2/PluginClient.cpp \
                 Collaboration2/VruiPluginClient.cpp \
                 Collaboration2/Client.cpp \
                 Collaboration2/CollaborativeVruiApplication.cpp

$(call LIBOBJNAMES,$(CLIENT_SOURCES)): | $(DEPDIR)/config

# The client-side library:
$(call LIBRARYNAME,libCollaboration2Client): PACKAGES = $(MYCOLLABORATION2CLIENT_DEPENDS)
$(call LIBRARYNAME,libCollaboration2Client): $(call LIBOBJNAMES,$(CLIENT_SOURCES))
.PHONY: libCollaboration2Client
libCollaboration2Client: $(call LIBRARYNAME,libCollaboration2Client)

# Make all client components depend on collaboration client library:
$(PLUGIN_CLIENTS) $(VISLETS) $(EXEDIR)/VruiCoreTest: | $(call LIBRARYNAME,libCollaboration2Client)

# Implicit rule to link client-side plug-ins:
$(call PLUGINNAME,%-Client): PACKAGES += MYCOLLABORATION2CLIENT
$(call PLUGINNAME,%-Client): HOSTPACKAGES += MYCOLLABORATION2CLIENT
$(call PLUGINNAME,%-Client):
	@mkdir -p $(PLUGINDESTDIR)
ifdef SHOWCOMMAND
	$(CCOMP) $(PLUGINLINKFLAGS) -o $@ $^ $(PLUGINLFLAGS)
else
	@echo Linking $@...
	@$(CCOMP) $(PLUGINLINKFLAGS) -o $@ $^ $(PLUGINLFLAGS)
endif

# The simple text chat protocol:
$(call PLUGINNAME,Chat.$(CHAT_VERSION)-Client): PACKAGES = MYTHREADS MYMISC
$(call PLUGINNAME,Chat.$(CHAT_VERSION)-Client): $(call MYPLUGINOBJNAMES,ChatProtocol.cpp ChatClient.cpp)

# The data object sharing protocol:
$(call PLUGINNAME,Koinonia.$(KOINONIA_VERSION)-Client): PACKAGES = MYTHREADS MYMISC
$(call PLUGINNAME,Koinonia.$(KOINONIA_VERSION)-Client): $(call MYPLUGINOBJNAMES,KoinoniaProtocol.cpp KoinoniaClient.cpp)

# The simple group audio protocol:
$(call PLUGINNAME,Agora.$(AGORA_VERSION)-Client): PACKAGES = MYCOLLABORATION2CLIENT MYSOUND MYTHREADS MYMISC OPUS PULSEAUDIO
$(call PLUGINNAME,Agora.$(AGORA_VERSION)-Client): $(call MYPLUGINOBJNAMES,AgoraProtocol.cpp \
                                                                          AudioEncoder.cpp \
                                                                          AudioDecoder.cpp \
                                                                          AgoraClient.cpp)

# Packages used by the VruiCore client plug-in:
VRUICORECLIENT_PACKAGES = MYVRUI MYSCENEGRAPH MYGLMOTIF MYGLGEOMETRY MYGLSUPPORT MYGEOMETRY MYMISC

# The Vrui Core protocol:
$(call PLUGINNAME,VruiCore.$(VRUICORE_VERSION)-Client): PACKAGES = $(VRUICORECLIENT_PACKAGES)
$(call PLUGINNAME,VruiCore.$(VRUICORE_VERSION)-Client): HOSTPACKAGES = MYVRUI
$(call PLUGINNAME,VruiCore.$(VRUICORE_VERSION)-Client): $(call MYPLUGINOBJNAMES,VruiCoreProtocol.cpp VruiCoreClient.cpp)

# The Vrui-embedded group audio protocol:
$(call PLUGINNAME,VruiAgora.$(VRUIAGORA_VERSION)-Client): PACKAGES = MYALSUPPORT MYSOUND MYGLMOTIF MYGEOMETRY MYIO MYTHREADS MYMISC OPUS PULSEAUDIO $(VRUICORECLIENT_PACKAGES)
$(call PLUGINNAME,VruiAgora.$(VRUIAGORA_VERSION)-Client): HOSTPACKAGES = $(VRUICORECLIENT_PACKAGES)
$(call PLUGINNAME,VruiAgora.$(VRUIAGORA_VERSION)-Client): $(call MYPLUGINOBJNAMES,VruiAgoraProtocol.cpp VruiAgoraClient.cpp)

# The IK avatar protocol:
$(call PLUGINNAME,Ensomatosis.$(ENSOMATOSIS_VERSION)-Client): PACKAGES = MYVRUI MYSCENEGRAPH MYGLMOTIF MYGEOMETRY MYMISC $(VRUICORECLIENT_PACKAGES)
$(call PLUGINNAME,Ensomatosis.$(ENSOMATOSIS_VERSION)-Client): HOSTPACKAGES = $(VRUICORECLIENT_PACKAGES)
$(call PLUGINNAME,Ensomatosis.$(ENSOMATOSIS_VERSION)-Client): $(call MYPLUGINOBJNAMES,EnsomatosisProtocol.cpp EnsomatosisClient.cpp)

# The collaboration client vislet:
$(call PLUGINOBJNAMES,Collaboration2Vislet.cpp): | $(DEPDIR)/config
$(call VISLETNAME,Collaboration2): PACKAGES = MYCOLLABORATION2CLIENT MYVRUI
$(call VISLETNAME,Collaboration2): HOSTPACKAGES = MYVRUI
$(call VISLETNAME,Collaboration2): $(call PLUGINOBJNAMES,Collaboration2Vislet.cpp)
	@mkdir -p $(VISLETDESTDIR)
ifdef SHOWCOMMAND
	$(CCOMP) $(PLUGINLINKFLAGS) -o $@ $^ $(PLUGINLFLAGS)
else
	@echo Linking $@...
	@$(CCOMP) $(PLUGINLINKFLAGS) -o $@ $^ $(PLUGINLFLAGS)
endif

# Simple collaboration test application:
$(OBJDIR)/VruiCoreTest.o: | $(DEPDIR)/config
$(EXEDIR)/VruiCoreTest: PACKAGES = MYCOLLABORATION2CLIENT MYVRUI
$(EXEDIR)/VruiCoreTest: $(OBJDIR)/VruiCoreTest.o
.PHONY: VruiCoreTest
VruiCoreTest: $(EXEDIR)/VruiCoreTest

########################################################################
# Specify installation rules for header files, libraries, executables,
# configuration files, and shared files.
########################################################################

# Pseudo-target to dump Collaboration Infrastructure configuration settings
$(MAKECONFIGFILE): | $(DEPDIR)/config
	@echo Creating configuration makefile fragment...
	@echo '# Makefile fragment for Collaboration configuration options' > $(MAKECONFIGFILE)
	@echo '# Autogenerated by Collaboration installation on $(shell date)' >> $(MAKECONFIGFILE)
	@echo >> $(MAKECONFIGFILE)
	@echo '# Version information:'>> $(MAKECONFIGFILE)
	@echo 'COLLABORATION_VERSION = $(COLLABORATION_VERSION)' >> $(MAKECONFIGFILE)
	@echo 'COLLABORATION_NAME = $(COLLABORATION_NAME)' >> $(MAKECONFIGFILE)
	@echo >> $(MAKECONFIGFILE)
	@echo '# Installation directories:'>> $(MAKECONFIGFILE)
	@echo 'COLLABORATIONPLUGINDIREXT = $(COLLABORATIONPLUGINDIREXT)' >> $(MAKECONFIGFILE)
	@echo 'COLLABORATIONCONFIGDIREXT = $(COLLABORATIONCONFIGDIREXT)' >> $(MAKECONFIGFILE)
	@echo 'COLLABORATIONRESOURCEDIREXT = $(COLLABORATIONRESOURCEDIREXT)' >> $(MAKECONFIGFILE)

install: config
# Install all header files in HEADERINSTALLDIR:
	@echo Installing header files...
	@install -d $(HEADERINSTALLDIR)/Collaboration2
	@install -m u=rw,go=r Collaboration2/*.h $(HEADERINSTALLDIR)/Collaboration2
	@install -d $(HEADERINSTALLDIR)/Collaboration2/Plugins
	@install -m u=rw,go=r Collaboration2/Plugins/*.h $(HEADERINSTALLDIR)/Collaboration2/Plugins
# Install all library files in LIBINSTALLDIR:
	@echo Installing libraries...
	@install -d $(LIBINSTALLDIR)
	@install $(LIBRARIES) $(LIBINSTALLDIR)
	@echo Configuring run-time linker...
	$(foreach LIBNAME,$(LIBRARY_NAMES),$(call CREATE_SYMLINK,$(LIBNAME)))
# Install all protocol plug-ins in MYPLUGININSTALLDIR:
	@echo Installing protocol plug-ins...
	@install -d $(MYPLUGININSTALLDIR)
	@install $(PLUGINS) $(MYPLUGININSTALLDIR)
# Install all vislet plug-ins in PLUGININSTALLDIR/VRVISLETSDIREXT:
	@echo Installing vislet plugins...
	@install -d $(PLUGININSTALLDIR)/$(VRVISLETSDIREXT)
	@install $(VISLETS) $(PLUGININSTALLDIR)/$(VRVISLETSDIREXT)
# Install all binaries in EXECUTABLEINSTALLDIR:
	@echo Installing executables...
	@install -d $(EXECUTABLEINSTALLDIR)
	@install $(EXECUTABLES) $(EXECUTABLEINSTALLDIR)
# Install all configuration files in MYETCINSTALLDIR:
	@echo Installing configuration files...
	@install -d $(MYETCINSTALLDIR)
	@install -m u=rw,go=r etc/* $(MYETCINSTALLDIR)
# Install all resource files in MYSHAREINSTALLDIR:
	@echo Installing resource files...
	@install -d $(MYSHAREINSTALLDIR)
	@install -m u=rw,go=r share/* $(MYSHAREINSTALLDIR)
# Install the package and configuration files in MAKEINSTALLDIR:
	@echo Installing makefile fragments...
	@install -d $(MAKEINSTALLDIR)
	@install -m u=rw,go=r BuildRoot/Packages.Collaboration $(MAKEINSTALLDIR)
	@cp $(MAKECONFIGFILE) $(MAKEINSTALLDIR)/Configuration.Collaboration
	@chmod u=rw,go=r $(MAKEINSTALLDIR)/Configuration.Collaboration

uninstall:
	@echo Removing header files...
	@rm -rf $(HEADERINSTALLDIR)/Collaboration2
	@echo Removing libraries...
	@rm -f $(LIBRARIES:$(LIBDESTDIR)/%=$(LIBINSTALLDIR)/%)
	$(foreach LIBNAME,$(LIBRARY_NAMES),$(call DESTROY_SYMLINK,$(LIBNAME)))
	@echo Removing protocol plug-ins...
	@rm -rf $(MYPLUGININSTALLDIR)
	@echo Removing vislet plug-ins...
	@rm -f $(VISLETS:$(VISLETDESTDIR)/%=$(PLUGININSTALLDIR)/$(VRVISLETSDIREXT)/%)
	@echo Removing executables...
	@rm -f $(EXECUTABLES:$(EXEDIR)/%=$(EXECUTABLEINSTALLDIR)/%)
	@echo Removing configuration files...
	@rm -rf $(MYETCINSTALLDIR)
	@echo Removing resource files...
	@rm -rf $(MYSHAREINSTALLDIR)
	@echo Removing makefile fragments...
	@rm -f $(MAKEINSTALLDIR)/Packages.Collaboration $(MAKEINSTALLDIR)/Configuration.Collaboration
