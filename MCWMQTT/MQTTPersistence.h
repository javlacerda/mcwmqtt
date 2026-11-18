#ifndef _MCW_MQTT_PERSISTENCE_H
#define _MCW_MQTT_PERSISTENCE_H

/*
** MCWMQTT - Persistence-directory helpers
**
** By default, on-disk persistence for in-flight QoS1/QoS2 messages is
** stored under a "MCWMQTT/<clientID>" subfolder of the OS temp directory,
** so each distinct client id gets its own store. The developer can
** override this via $setpersistencedirectory.
**
** All paths are UTF-8 (the encoding produced by getStringFromEXTCompInfo).
*/

#include <string>

// Builds the default persistence directory (UTF-8):
// <system temp>/MCWMQTT/<sanitized clientID>/
std::string buildDefaultPersistenceDirectory(const std::string &clientID);

// Creates 'utf8Dir' (including any missing parent directories) and returns,
// in 'pahoDir', the form of the path that paho's file persistence can open
// on this platform. Returns false and fills 'error' on failure.
bool preparePersistenceDirectory(const std::string &utf8Dir, std::string &pahoDir, std::string &error);

#endif
