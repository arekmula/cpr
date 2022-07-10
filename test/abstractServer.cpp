#include "abstractServer.hpp"

namespace cpr {
void AbstractServer::SetUp() {
    Start();
}

void AbstractServer::TearDown() {
    Stop();
}

void AbstractServer::Start() {
    // See sigaction(2) and the example from mprotect(2)
    struct sigaction action {};
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = SignalHandler;
    sigaction(SIGPIPE, &action, nullptr);
    should_run = true;
    serverThread = std::make_shared<std::thread>(&AbstractServer::Run, this);
    serverThread->detach();
    std::unique_lock<std::mutex> server_lock(server_mutex);
    server_start_cv.wait(server_lock);
}

void AbstractServer::Stop() {
    should_run = false;
    std::unique_lock<std::mutex> server_lock(server_mutex);
    server_stop_cv.wait(server_lock);
}

static void EventHandler(mg_connection* conn, int event, void* event_data, void* context) {
    switch (event) {
        case MG_EV_READ:
        case MG_EV_WRITE:
            /** Do nothing. Just for housekeeping. **/
            break;
        case MG_EV_POLL:
            /** Do nothing. Just for housekeeping. **/
            break;
        case MG_EV_CLOSE:
            /** Do nothing. Just for housekeeping. **/
            break;
        case MG_EV_ACCEPT:
            /* Initialize HTTPS connection if Server is an HTTPS Server */
            static_cast<AbstractServer*>(context)->acceptConnection(conn);
            break;
        case MG_EV_CONNECT:
            /** Do nothing. Just for housekeeping. **/
            break;

        case MG_EV_HTTP_CHUNK: {
            /** Do nothing. Just for housekeeping. **/
        } break;

        case MG_EV_HTTP_MSG: {
            AbstractServer* server = static_cast<AbstractServer*>(context);
            server->OnRequest(conn, static_cast<mg_http_message*>(event_data));
        } break;

        default:
            break;
    }
}

void AbstractServer::Run() {
    // Setup a new mongoose http server.
    memset(&mgr, 0, sizeof(mg_mgr));
    initServer(&mgr, EventHandler);

    // Notify the main thread that the server is up and runing:
    server_start_cv.notify_all();

    // Main server loop:
    while (should_run) {
        // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
        mg_mgr_poll(&mgr, 1000);
    }

    // Shutdown and cleanup:
    mg_mgr_free(&mgr);

    // Notify the main thread that we have shut down everything:
    server_stop_cv.notify_all();
}

void AbstractServer::SignalHandler(int /*signo*/, siginfo_t* /*si*/, void* /*ptr*/) {
    // Do nothing, only for handling of SIGPIPE
    std::cout << "Caught SIGPIPE" << '\n';
}

static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
/**
 * Decodes the given BASE64 string to a normal string.
 * Source: https://gist.github.com/williamdes/308b95ac9ef1ee89ae0143529c361d37
 **/
std::string AbstractServer::Base64Decode(const std::string& in) {
    std::string out;

    // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
    std::vector<int> T(256, -1);
    // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
    for (size_t i = 0; i < 64; i++)
        T[base64_chars[i]] = i;

    int val = 0;
    // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
    int valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) {
            break;
        }
        // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
        val = (val << 6) + T[c];
        // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
        valb += 6;
        if (valb >= 0) {
            // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
            out.push_back(char((val >> valb) & 0xFF));
            // NOLINTNEXTLINE (cppcoreguidelines-avoid-magic-numbers)
            valb -= 8;
        }
    }
    return out;
}

// Sends error similar like in mongoose 6 method mg_http_send_error
// https://github.com/cesanta/mongoose/blob/6.18/mongoose.c#L7081-L7089
void AbstractServer::SendError(mg_connection* conn, int code, std::string& reason) {
    std::string headers{"Content-Type: text/plain\r\nConnection: close\r\n"};
    mg_http_reply(conn, code, headers.c_str(), reason.c_str());
}

} // namespace cpr
