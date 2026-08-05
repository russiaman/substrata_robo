/*=====================================================================
ThreadMessages.h
----------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <ThreadMessage.h>
#include <string>


enum GuiClientThreadMessages
{
	Msg_ModelLoadedThreadMessage,
	Msg_TextureLoadedThreadMessage,
	Msg_AudioLoadedThreadMessage,
	Msg_ScriptLoadedThreadMessage,
	Msg_ClientConnectingToServerMessage,
	Msg_ClientConnectedToServerMessage,
	Msg_AudioStreamToServerStartedMessage,
	Msg_AudioStreamToServerEndedMessage,
	Msg_RemoteClientAudioStreamToServerStarted,
	Msg_RemoteClientAudioStreamToServerEnded,
	Msg_ClientProtocolTooOldMessage,
	Msg_ClientDisconnectedFromServerMessage,
	Msg_AvatarIsHereMessage,
	Msg_AvatarCreatedMessage,
	Msg_AvatarPerformGestureMessage,
	Msg_AvatarStopGestureMessage,
	Msg_ChatMessage,
	Msg_ScriptedObMoveToMessage,
	Msg_InfoMessage,
	Msg_ErrorMessage,
	Msg_LogMessage,
	Msg_LoggedInMessage,
	Msg_LoggedOutMessage,
	Msg_SignedUpMessage,
	Msg_ServerAdminMessage,
	Msg_WorldSettingsReceivedMessage,
	Msg_WorldDetailsReceivedMessage,
	Msg_MapTilesResultReceivedMessage,
	Msg_UserSelectedObjectMessage,
	Msg_UserDeselectedObjectMessage,
	Msg_GetFileMessage,
	Msg_NewResourceOnServerMessage,
	Msg_ResourceDownloadedMessage,
	Msg_TerrainChunkGeneratedMsg,
	Msg_WindNoiseLoaded,
	Msg_UserGearListMessage,
	Msg_GaussianSplatLodBuildStatusMessage,
	Msg_BuilderAITextDeltaMessage,
	Msg_BuilderAIToolActivityMessage,
	Msg_BuilderAITurnCompleteMessage,
	Msg_BuilderAIErrorMessage,
	Msg_TextureUploadedMessage = 1000, // Should match the values from <opengl/OpenGLUploadThread.h>
	Msg_AnimatedTextureUpdated = 1001,
	Msg_GeometryUploadedMessage = 1002,
	Msg_OpenGLUploadErrorMessage = 1003,
};


class LogMessage : public ThreadMessage
{
public:
	LogMessage(const std::string& msg_) : ThreadMessage(Msg_LogMessage), msg(msg_) {}
	std::string msg;
};


class InfoMessage : public ThreadMessage
{
public:
	InfoMessage(const std::string& msg_) : ThreadMessage(Msg_InfoMessage), msg(msg_) {}
	std::string msg;
};


class ErrorMessage : public ThreadMessage
{
public:
	ErrorMessage(const std::string& msg_) : ThreadMessage(Msg_ErrorMessage), msg(msg_) {}
	std::string msg;
};


// Sent by LoadModelTask (see its .cpp) around a Gaussian Splat LoD tree build, so GUIClient can show/hide the "Building..." indicator (UIInterface::setGaussianSplatLodBuildInProgress()) for the duration, without
// GUIClient needing to know anything about how/when LoadModelTask decides to build a tree. Always sent in starting=true, starting=false pairs for a given load - see LoadModelTask.cpp's .sog branch for why a
// tree-build failure can't leave a starting=true without a matching starting=false.
class GaussianSplatLodBuildStatusMessage : public ThreadMessage
{
public:
	GaussianSplatLodBuildStatusMessage(bool starting_) : ThreadMessage(Msg_GaussianSplatLodBuildStatusMessage), starting(starting_) {}
	bool starting;
};


//---------------- Builder AI messages, posted by ClientThread to the GUIClient ----------------

class BuilderAITextDeltaMessage : public ThreadMessage
{
public:
	BuilderAITextDeltaMessage(const std::string& text_) : ThreadMessage(Msg_BuilderAITextDeltaMessage), text(text_) {}
	std::string text;
};


class BuilderAIToolActivityMessage : public ThreadMessage
{
public:
	BuilderAIToolActivityMessage(const std::string& tool_name_) : ThreadMessage(Msg_BuilderAIToolActivityMessage), tool_name(tool_name_) {}
	std::string tool_name;
};


class BuilderAITurnCompleteMessage : public ThreadMessage
{
public:
	BuilderAITurnCompleteMessage() : ThreadMessage(Msg_BuilderAITurnCompleteMessage) {}
};


class BuilderAIErrorMessage : public ThreadMessage
{
public:
	BuilderAIErrorMessage(const std::string& msg_) : ThreadMessage(Msg_BuilderAIErrorMessage), msg(msg_) {}
	std::string msg;
};
