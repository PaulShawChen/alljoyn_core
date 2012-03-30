/**
 * @file
 * ClientTransport is an implementation of Transport that listens
 * on an AF_UNIX socket.
 */

/******************************************************************************
 * Copyright 2009-2011, Qualcomm Innovation Center, Inc.
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

#include <qcc/platform.h>

#include <list>

#include <errno.h>
#include <qcc/Socket.h>
#include <qcc/SocketStream.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/Util.h>
#include <sys/un.h>
#include <sys/ucred.h>

#include <alljoyn/BusAttachment.h>

#include "BusInternal.h"
#include "RemoteEndpoint.h"
#include "Router.h"
#include "ClientTransport.h"
#include <qcc/StringUtil.h>

#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;

namespace ajn {

const char* ClientTransport::TransportName = "launchd";

class ClientEndpoint : public RemoteEndpoint {
  public:
    /* Unix endpoint constructor */
    ClientEndpoint(BusAttachment& bus, bool incoming, const qcc::String connectSpec, SocketFd sock) :
        RemoteEndpoint(bus, incoming, connectSpec, stream, ClientTransport::TransportName),
        userId(-1),
        groupId(-1),
        processId(-1),
        stream(sock)
    {
    }

    /* Destructor */
    virtual ~ClientEndpoint() { }

    /**
     * Set the user id of the endpoint.
     *
     * @param   userId      User ID number.
     */
    void SetUserId(uint32_t userId) { this->userId = userId; }

    /**
     * Set the group id of the endpoint.
     *
     * @param   groupId     Group ID number.
     */
    void SetGroupId(uint32_t groupId) { this->groupId = groupId; }

    /**
     * Set the process id of the endpoint.
     *
     * @param   processId   Process ID number.
     */
    void SetProcessId(uint32_t processId) { this->processId = processId; }

    /**
     * Return the user id of the endpoint.
     *
     * @return  User ID number.
     */
    uint32_t GetUserId() const { return userId; }

    /**
     * Return the group id of the endpoint.
     *
     * @return  Group ID number.
     */
    uint32_t GetGroupId() const { return groupId; }

    /**
     * Return the process id of the endpoint.
     *
     * @return  Process ID number.
     */
    uint32_t GetProcessId() const { return processId; }

    /**
     * Indicates if the endpoint supports reporting UNIX style user, group, and process IDs.
     *
     * @return  'true' if UNIX IDs supported, 'false' if not supported.
     */
    bool SupportsUnixIDs() const { return true; }


  private:
    uint32_t userId;
    uint32_t groupId;
    uint32_t processId;
    SocketStream stream;
};

QStatus ClientTransport::NormalizeTransportSpec(const char* inSpec, qcc::String& outSpec, map<qcc::String, qcc::String>& argMap) const
{
    qcc::String normSpec;
    /*
     * Take the string in inSpec, which must start with "launchd:" and parse it,
     * looking for comma-separated "key=value" pairs and initialize the
     * argMap with those pairs.
     */
    QStatus status = ParseArguments("launchd", inSpec, argMap);
    if (status != ER_OK) {
        return status;
    }

    qcc::String env = Trim(argMap["env"]);
    normSpec = "launchd:";
    if (env.empty()) {
        env = "DBUS_LAUNCHD_SESSION_BUS_SOCKET";
    }
    normSpec.append("env=");
    normSpec.append(env);
    argMap["_spec"] = env;

    /*
     * 'launchctl getenv DBUS_LAUNCHD_SESSION_BUS_SOCKET' will return a blank line, or a line containing
     * the Unix domain socket path.
     */
    qcc::String command = "launchctl getenv " + argMap["_spec"];
    FILE* launchctl = popen(command.c_str(), "r");
    if (!launchctl) {
        status = ER_OS_ERROR;
        QCC_LogError(status, ("launchctl failed"));
        return status;
    }
    outSpec = "unix:path=";
    char buf[256];
    while (!feof(launchctl) && !ferror(launchctl)) {
        size_t n = fread(buf, 1, 255, launchctl);
        buf[n] = 0;
        outSpec += buf;
    }
    outSpec = Trim(outSpec);
    if (ferror(launchctl)) {
        status = ER_OS_ERROR;
        QCC_LogError(status, ("launchctl failed"));
    }
    pclose(launchctl);

    status = ParseArguments("unix", outSpec.c_str(), argMap);
    if (status != ER_OK) {
        return status;
    }

    qcc::String path = Trim(argMap["path"]);
    qcc::String abstract = Trim(argMap["abstract"]);
    if (ER_OK == status) {
        // @@ TODO: Path normalization?
        outSpec = "unix:";
        if (!path.empty()) {
            outSpec.append("path=");
            outSpec.append(path);
            argMap["_spec"] = path;
        } else if (!abstract.empty()) {
            outSpec.append("abstract=");
            outSpec.append(abstract);
            argMap["_spec"] = qcc::String("@") + abstract;
        } else {
            status = ER_BUS_BAD_TRANSPORT_ARGS;
        }
    }

    return status;
}

QStatus ClientTransport::Connect(const char* connectArgs, const SessionOpts& opts, RemoteEndpoint** newep)
{
    /*
     * Don't bother trying to create a new endpoint if the state precludes them.
     */
    if (m_running == false || m_stopping == true) {
        return ER_BUS_TRANSPORT_NOT_STARTED;
    }
    if (m_endpoint) {
        return ER_BUS_ALREADY_CONNECTED;
    }

    /*
     * Parse and normalize the connectArgs.  For a client or service, there are
     * no reasonable defaults and so the addr and port keys MUST be present or
     * an error is returned.
     */
    QStatus status;
    qcc::String normSpec;
    map<qcc::String, qcc::String> argMap;
    status = ClientTransport::NormalizeTransportSpec(connectArgs, normSpec, argMap);
    if (ER_OK != status) {
        QCC_LogError(status, ("ClientTransport::Connect(): Invalid Unix connect spec \"%s\"", connectArgs));
        return status;
    }
    /*
     * Attempt to connect to the endpoint specified in the connectSpec.
     */
    SocketFd sockFd = -1;
    status = Socket(QCC_AF_UNIX, QCC_SOCK_STREAM, sockFd);
    if (status != ER_OK) {
        QCC_LogError(status, ("ClientTransport(): socket Create() failed"));
        return status;
    }
    /*
     * Got a socket, now Connect() to it.
     */
    qcc::String& spec = argMap["_spec"];
    status = qcc::Connect(sockFd, spec.c_str());
    if (status != ER_OK) {
        QCC_LogError(status, ("ClientTransport(): socket Connect(%d, %s) failed", sockFd, spec.c_str()));
        qcc::Close(sockFd);
        return status;
    }

    if (m_stopping) {
        status = ER_BUS_TRANSPORT_NOT_STARTED;
    } else {
        m_endpoint = new ClientEndpoint(m_bus, false, normSpec, sockFd);

        /* Initialized the features for this endpoint */
        m_endpoint->GetFeatures().isBusToBus = false;
        m_endpoint->GetFeatures().allowRemote = m_bus.GetInternal().AllowRemoteMessages();
        m_endpoint->GetFeatures().handlePassing = true;

        qcc::String authName;
        qcc::String redirection;
        status = m_endpoint->Establish("EXTERNAL", authName, redirection);
        if (status == ER_OK) {
            m_endpoint->SetListener(this);
            status = m_endpoint->Start();
            if (status != ER_OK) {
                QCC_LogError(status, ("ClientTransport::Connect(): Start ClientEndpoint failed"));
            }
        }
    }
    /*
     * If we got an error, we need to cleanup the socket and zero out the
     * returned endpoint.  If we got this done without a problem, we return
     * a pointer to the new endpoint.
     */
    if (status != ER_OK) {
        m_stopping = true;
        if (m_endpoint) {
            delete m_endpoint;
            m_endpoint = NULL;
        }
        qcc::Shutdown(sockFd);
        qcc::Close(sockFd);
    }
    if (newep) {
        *newep = m_endpoint;
    }
    return status;
}

} // namespace ajn