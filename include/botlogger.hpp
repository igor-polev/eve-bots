/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

    BotLogger class definition and implementation.
*/

#pragma once
#include <string>
#include <iostream>
#include <curl/curl.h>

using namespace std;

class BotLogger {
public:
    enum log_t {
        INFO,
        WARNING,
        ERROR,
        CRITICAL,
        NONE
    };
    enum log_dst_t {
        CONSOLE  = 0b01,
        TELEGRAM = 0b10,
        ALL      = 0b11
    };
    BotLogger() = default;
    BotLogger(const char*   tl_bot_token, const char*   tl_chat_id);
    BotLogger(const string& tl_bot_token, const string& tl_chat_id);
    ~BotLogger();
    void set_sender(const char*   sender);
    void set_sender(const string& sender);
    void telegram_switch(bool state) noexcept;
    void write(
        const string& msg,
        log_t     type = INFO,
        log_dst_t dst  = ALL
    ) const;
private:
    bool telegram_message(const string& msg) const;
    bool telegram_message(const char*   msg) const;
    
    string m_chat_str  {},
           m_sender    {};
    CURL  *mp_curl     {nullptr};
    bool   m_tl_switch {true};
};

///////////////////////////////////////////////////////////////
// inline methods implementation

//"8043028444:AAG2PGAHnw90aQSLOlgeuaIMw1PPLXo85uc"
inline BotLogger::BotLogger(const string& tl_bot_token, const string& tl_chat_id)
    : BotLogger(tl_bot_token.c_str(), tl_chat_id.c_str()) {}

inline BotLogger::BotLogger(const char* tl_bot_token, const char* tl_chat_id)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    mp_curl = curl_easy_init();
    if (!mp_curl) {
        curl_global_cleanup();
        write(
            "failed to init curl - telegram logging disabled",
            WARNING,
            CONSOLE
        );
        return;
    }
    curl_easy_setopt(
        mp_curl,
        CURLOPT_URL,
        (
            string("https://api.telegram.org/bot")
            + tl_bot_token
            + "/sendMessage"
        ).c_str()
    );
    curl_easy_setopt(
        mp_curl,
        CURLOPT_HTTPHEADER,
        (struct curl_slist*)curl_slist_append(
            NULL,
            "Content-Type: application/x-www-form-urlencoded"
        )
    );
    m_chat_str = string("chat_id=") + tl_chat_id + "&text=";
}

inline BotLogger::~BotLogger()
{
    if (mp_curl) {
        curl_easy_cleanup(mp_curl);
        curl_global_cleanup();
    }
}
inline void BotLogger::telegram_switch(bool state) noexcept
{
    m_tl_switch = state;
}

inline void BotLogger::set_sender(const char* sender)
{
    set_sender(string(sender));
}
inline void BotLogger::set_sender(const string& sender)
{
    m_sender = " > " + sender;
}

inline bool BotLogger::telegram_message(const string& msg) const
{
	return telegram_message(msg.c_str());
}
inline bool BotLogger::telegram_message(const char* msg) const
{
    if (!mp_curl) return false;
    curl_easy_setopt(
        mp_curl,
        CURLOPT_POSTFIELDS,
        (m_chat_str + msg).c_str()
    );
    return CURLE_OK == curl_easy_perform(mp_curl);
}

inline void BotLogger::write(
    const string& msg,
    log_t         type,
    log_dst_t     dst) const 
{
    static ostream *out = &(
        ERROR == type || type == CRITICAL
        ? cerr : cout
    );
    static string message;
    switch (type) {
        case INFO:
            message = "    [INFO";
            break;
        case WARNING:
            message = " [WARNING";
            break;
        case ERROR:
            message = "   [ERROR";
            break;
        case CRITICAL:
            message = "[CRITICAL";
            break;
        case NONE:
        default:
            message = "[";
    }
    message += m_sender + "] " + msg;
    if (dst & CONSOLE)
        *out << message << endl << flush;
    if (m_tl_switch && (dst & TELEGRAM))
        telegram_message(message);
}
