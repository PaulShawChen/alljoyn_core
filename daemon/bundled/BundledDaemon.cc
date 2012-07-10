/**
 * @file
 * Implementation of class for launching a bundled daemon
 */

/******************************************************************************
 * Copyright 2012, Qualcomm Innovation Center, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 ******************************************************************************/

#include <stdio.h>

#include <qcc/platform.h>
#include <qcc/Debug.h>
#include <qcc/Logger.h>
#include <qcc/Log.h>
#include <qcc/String.h>
#include <qcc/StringSource.h>
#include <qcc/StringUtil.h>
#include <qcc/Mutex.h>
#include <qcc/ScopedMutexLock.h>
#include <qcc/FileStream.h>

#include <alljoyn/BusAttachment.h>

#include <Status.h>

#include "Bus.h"
#include "BusController.h"
#include "DaemonConfig.h"
#include "Transport.h"
#include "TCPTransport.h"
#include "NullTransport.h"

#if defined(QCC_OS_ANDROID) || defined(QCC_OS_LINUX)
#include "DaemonICETransport.h"
#endif

#define QCC_MODULE "ALLJOYN_DAEMON"

using namespace qcc;
using namespace std;
using namespace ajn;

static const char bundledConfig[] =
    "<busconfig>"
    "  <type>alljoyn_bundled</type>"
    "  <listen>tcp:addr=0.0.0.0,port=0,family=ipv4</listen>"
    "  <limit name=\"auth_timeout\">5000</limit>"
    "  <limit name=\"max_incomplete_connections_tcp\">4</limit>"
    "  <limit name=\"max_completed_connections_tcp\">16</limit>"
    "  <ip_name_service>"
    "    <property interfaces=\"*\"/>"
    "    <property disable_directed_broadcast=\"false\"/>"
    "    <property enable_ipv4=\"true\"/>"
    "    <property enable_ipv6=\"true\"/>"
    "  </ip_name_service>"
#if defined(QCC_OS_ANDROID) || defined(QCC_OS_LINUX)
    "  <listen>ice:</listen>"
    "  <ice>"
    "    <limit name=\"max_incomplete_connections\">16</limit>"
    "    <limit name=\"max_completed_connections\">64</limit>"
    "  </ice>"
    "  <ice_discovery_manager>"
    "    <property interfaces=\"*\"/>"
    "    <property server=\"rdvs.alljoyn.org\"/>"
    "    <property protocol=\"HTTP\"/>"
    "    <property enable_ipv6=\"false\"/>"
    "  </ice_discovery_manager>"
#endif
    "</busconfig>";

class BundledDaemon : public DaemonLauncher, public TransportFactoryContainer {

  public:

    BundledDaemon();

    /**
     * Launch the bundled daemon
     */
    QStatus Start(NullTransport* nullTransport);

    /**
     * Terminate the bundled daemon
     */
    QStatus Stop();

    /**
     * Wait for bundled daemon to exit
     */
    void Join();

  private:

    bool transportsInitialized;
    volatile int32_t refCount;
    Bus* ajBus;
    BusController* ajBusController;
    Mutex lock;

};

bool ExistFile(const char* fileName) {
    FILE* file = NULL;
    if (fileName && (file = fopen(fileName, "r"))) {
        fclose(file);
        return true;
    }
    return false;
}

/*
 * Create the singleton bundled daemon instance.
 */
static BundledDaemon bundledDaemon;

BundledDaemon::BundledDaemon() : transportsInitialized(false), refCount(0), ajBus(NULL), ajBusController(NULL)
{
    NullTransport::RegisterDaemonLauncher(this);
}

QStatus BundledDaemon::Start(NullTransport* nullTransport)
{
    QStatus status = ER_OK;

    printf("Using BundledDaemon\n");

    /*
     * Need a mutex around this to prevent more than one BusAttachment from bringing up the
     * bundled daemon at the same time.
     */
    ScopedMutexLock guard(lock);

    if (IncrementAndFetch(&refCount) == 1) {
        LoggerSetting::GetLoggerSetting("bundled-daemon", LOG_DEBUG, false, stdout);
        /*
         * Load the configuration
         */
        DaemonConfig* config = NULL;
#ifndef NDEBUG
        qcc::String configFile = qcc::String::Empty;
    #if defined(QCC_OS_ANDROID)
        configFile = "/mnt/sdcard/.alljoyn/config.xml";
    #endif

    #if defined(QCC_OS_LINUX) || defined(QCC_OS_WINDOWS)
        configFile = "./config.xml";
    #endif

        if (!configFile.empty() && ExistFile(configFile.c_str())) {
            FileSource fs(configFile);
            if (fs.IsValid()) {
                config = DaemonConfig::Load(fs);
                if (!config) {
                    status = ER_BUS_BAD_XML;
                    QCC_LogError(status, ("Error parsing configuration from %s", configFile.c_str()));
                    goto ErrorExit;
                }
            }
        }
#endif
        if (!config) {
            config = DaemonConfig::Load(bundledConfig);
        }
        if (!config) {
            status = ER_BUS_BAD_XML;
            QCC_LogError(status, ("Error parsing configuration"));
            goto ErrorExit;
        }
        /*
         * Extract the listen specs
         */
        vector<String> listenList = config->GetList("listen");
        String listenSpecs = StringVectorToString(&listenList, ";");
        /*
         * Register the transport factories - this is a one time operation
         */
        if (!transportsInitialized) {
            Add(new TransportFactory<TCPTransport>(TCPTransport::TransportName, false));
#if defined(QCC_OS_ANDROID) || defined(QCC_OS_LINUX)
            Add(new TransportFactory<DaemonICETransport>(DaemonICETransport::TransportName, false));
#endif
            transportsInitialized = true;
        }
        QCC_DbgPrintf(("Starting bundled daemon bus attachment"));
        /*
         * Create and start the daemon
         */
        ajBus = new Bus("bundled-daemon", *this, listenSpecs.c_str());
        ajBusController = new BusController(*ajBus);
        status = ajBusController->Init(listenSpecs);
        if (ER_OK != status) {
            goto ErrorExit;
        }
    }
    /*
     * Use the null transport to link the daemon and client bus together
     */
    status = nullTransport->LinkBus(ajBus);
    if (status != ER_OK) {
        goto ErrorExit;
    }

    return ER_OK;

ErrorExit:

    if (DecrementAndFetch(&refCount) == 0) {
        delete ajBusController;
        ajBusController = NULL;
        delete ajBus;
        ajBus = NULL;
    }
    return status;
}

void BundledDaemon::Join()
{
    ScopedMutexLock guard(lock);
    if (refCount == 0) {
        if (ajBus) {
            QCC_DbgPrintf(("Joining bundled daemon bus attachment"));
            ajBus->Join();
        }
        delete ajBusController;
        ajBusController = NULL;
        delete ajBus;
        ajBus = NULL;
    }
}

QStatus BundledDaemon::Stop()
{
    ScopedMutexLock guard(lock);
    int32_t rc = DecrementAndFetch(&refCount);
    assert(rc >= 0);

    if ((rc == 0) && ajBus) {
        QCC_DbgPrintf(("Stopping bundled daemon bus attachment"));
        return ajBus->Stop();
    }
    return ER_OK;
}
