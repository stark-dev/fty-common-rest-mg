/*
 *
 * Copyright (C) 2015 - 2020 Eaton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*!
 * \file fty_common_rest_utils_web.cc
 * \author Karol Hrdina <KarolHrdina@Eaton.com>
 * \author Arnaud Quette <ArnaudQuette@Eaton.com>
 * \brief Not yet documented file
 */

#include <cstring>
#include <cxxtools/jsondeserializer.h>
#include <cxxtools/jsonformatter.h>
#include <cxxtools/serializationinfo.h>
#include <fty_common_utf8.h>
#include <limits>
#include <mutex>
#include <ostream>
#include <regex>
#include <stdlib.h> // for random()
#include <sys/syscall.h>
#include <fty/string-utils.h>

//#include "shared/subprocess.h"
#include "fty_common_rest_utils_web.h"

namespace utils {

static uintmax_t get_current_pthread_id(void)
{
    int       sp = sizeof(pthread_t), si = sizeof(uintmax_t);
    uintmax_t thread_id = pthread_self();

    if (sp < si) {
        // Zero-out extra bits that an uintmax_t value could
        // have populated above the smaller pthread_t value
        int shift_bits = (si - sp) << 3; // * 8
        thread_id      = ((thread_id << shift_bits) >> shift_bits);
    }

    return thread_id;
}

// This routine aims to return LWP number wherever we
// know how to get its ID.
static uintmax_t get_current_thread_id(void)
{
    // Note: implementations below all assume that the routines
    // return a numeric value castable to uintmax_t, and should
    // fail during compilation otherwise so we can then fix it.
    uintmax_t thread_id = 0;
#if defined(__linux__)
    // returned actual type: long
    thread_id = uintmax_t(syscall(SYS_gettid));
#elif defined(__FreeBSD__)
    // returned actual type: int
    thread_id = pthread_getthreadid_np();
#elif defined(__sun)
    // TODO: find a way to extract the LWP ID in current PID
    // It is known ways exist for Solaris 8-11...
    // returned actual type: pthread_t (which is uint_t in Sol11 at least)
    thread_id = pthread_self();
#elif defined(__APPLE__)
    pthread_t temp_thread_id;
    (void)pthread_threadid_np(pthread_self(), &temp_thread_id);
    thread_id = temp_thread_id;
#else
#error "Platform not supported: get_current_thread_id() implementation missing"
#endif
    return thread_id;
}

static char* asprintf_thread_id(void)
{
    uintmax_t process_id = uintmax_t(getpid());
    uintmax_t pthread_id = get_current_pthread_id();
    uintmax_t thread_id  = get_current_thread_id();
    char*     buf        = nullptr;
    char*     buf2       = nullptr;

    if (pthread_id != thread_id) {
        if (0 > asprintf(&buf2, ".%ju", thread_id)) {
            if (buf2) {
                free(buf2);
                buf2 = nullptr;
            }
        }
    }

    if (0 > asprintf(&buf, "%ju.%ju%s", process_id, pthread_id, buf2 ? buf2 : "")) {
        if (buf) {
            free(buf);
            buf = nullptr;
        }
    }

    if (buf2) {
        free(buf2);
        buf2 = nullptr;
    }

    if (buf == nullptr)
        buf = strdup("");

    return buf;
}

uint32_t string_to_element_id(const std::string& string)
{

    size_t             pos = 0;
    unsigned long long u   = std::stoull(string, &pos);
    if (pos != string.size()) {
        throw std::invalid_argument("string_to_element_id");
    }
    if (u == 0 || u > std::numeric_limits<uint32_t>::max()) {
        throw std::out_of_range("string_to_element_id");
    }
    uint32_t element_id = static_cast<uint32_t>(u);
    return element_id;
}

std::string generate_mlm_client_id(std::string client_name)
{
    /* TODO: Would a global semaphored incrementable value make
     * sense here as part of the unique name of an MQ client? */
    char* pidstr = asprintf_thread_id();
    if (pidstr) {
        client_name.append(".").append(pidstr);
        free(pidstr);
        pidstr = nullptr;
    } else {
        long r = random();
        client_name.append(".").append(std::to_string(r));
    }
    return client_name;
}

namespace json {


    // Note: At the time of re-writing from defect cxxtools::JsonFormatter I decided
    // to go with the simplest solution for create_error_json functions (after all,
    // our error template is not that complex). Shall some person find this ugly
    // and repulsive enough, there is a working cxxtools::JsonSerialize solution
    // ready at the following link:
    // http://stash.mbt.lab.etn.com/projects/BIOS/repos/core/pull-requests/1094/diff#src/web/src/error.cc

    std::string create_error_json(const std::string& message, uint32_t code)
    {
        return std::string(
            "{\n"
            "\t\"errors\": [\n"
            "\t\t{\n"
            "\t\t\t\"message\": ")
            .append(jsonify(message))
            .append(
                ",\n"
                "\t\t\t\"code\": ")
            .append(jsonify(code))
            .append(
                "\n"
                "\t\t}\n"
                "\t]\n"
                "}\n");
    }

    std::string create_error_json(const std::string& message, uint32_t code, const std::string& debug)
    {
        return std::string(
            "{\n"
            "\t\"errors\": [\n"
            "\t\t{\n"
            "\t\t\t\"message\": ")
            .append(jsonify(message))
            .append(
                ",\n"
                "\t\t\t\"debug\": ")
            .append(jsonify(debug))
            .append(
                ",\n"
                "\t\t\t\"code\": ")
            .append(jsonify(code))
            .append(
                "\n"
                "\t\t}\n"
                "\t]\n"
                "}\n");
    }

    std::string create_error_json(std::vector<std::tuple<uint32_t, std::string, std::string>> messages)
    {
        std::string result =
            "{\n"
            "\t\"errors\": [\n";

        for (const auto& it : messages) {
            result.append(
                "\t\t{\n"
                "\t\t\t\"message\": ");
            result.append(jsonify(std::get<1>(it))).append(",\n");
            if (std::get<2>(it) != "") {
                result.append("\t\t\t\"debug\": ");
                result.append(jsonify(std::get<2>(it))).append(",\n");
            }
            result.append("\t\t\t\"code\": ");
            result.append(jsonify(std::get<0>(it)))
                .append(
                    "\n"
                    "\t\t},\n");
        }
        // remove ,\n
        result.pop_back();
        result.pop_back();

        result.append(
            "\n\t]\n"
            "}\n");
        return result;
    }

    std::string jsonify(double t)
    {
        if (std::isnan(t))
            return "nullptr";
        return std::to_string(t);
    }

} // namespace json

namespace config {
    const char* get_mapping(const std::string& key)
    {
        const static std::map<const std::string, const std::string> config_mapping = {// general
            {"BIOS_SNMP_COMMUNITY_NAME", "snmp/community"},
            // nut
            {"BIOS_NUT_POLLING_INTERVAL", "nut/polling_interval"},
            // agent-smtp
            {"language", "server/language"}, {"BIOS_SMTP_SERVER", "smtp/server"}, {"BIOS_SMTP_PORT", "smtp/port"},
            {"BIOS_SMTP_ENCRYPT", "smtp/encryption"}, {"BIOS_SMTP_VERIFY_CA", "smtp/verify_ca"},
            {"BIOS_SMTP_USER", "smtp/user"}, {"BIOS_SMTP_PASSWD", "smtp/password"}, {"BIOS_SMTP_FROM", "smtp/from"},
            {"BIOS_SMTP_SMS_GATEWAY", "smtp/smsgateway"}, {"BIOS_SMTP_USE_AUTHENTICATION", "smtp/use_auth"},
            // agent-ms
            {"BIOS_METRIC_STORE_AGE_RT", "store/rt"}, {"BIOS_METRIC_STORE_AGE_15m", "store/15m"},
            {"BIOS_METRIC_STORE_AGE_30m", "store/30m"}, {"BIOS_METRIC_STORE_AGE_1h", "store/1h"},
            {"BIOS_METRIC_STORE_AGE_8h", "store/8h"}, {"BIOS_METRIC_STORE_AGE_24h", "store/24h"},
            {"BIOS_METRIC_STORE_AGE_7d", "store/7d"}, {"BIOS_METRIC_STORE_AGE_30d", "store/30d"},
            // fty-discovery
            {"FTY_DISCOVERY_TYPE", "discovery/type"}, {"FTY_DISCOVERY_SCANS", "discovery/scans"},
            {"FTY_DISCOVERY_IPS", "discovery/ips"}, {"FTY_DISCOVERY_DOCUMENTS", "discovery/documents"},
            {"FTY_DISCOVERY_DEFAULT_VALUES_STATUS", "discovery/defaultValuesAux/status"},
            {"FTY_DISCOVERY_DEFAULT_VALUES_PRIORITY", "discovery/defaultValuesAux/priority"},
            {"FTY_DISCOVERY_DEFAULT_VALUES_PARENT", "discovery/defaultValuesAux/parent"},
            {"FTY_DISCOVERY_DEFAULT_VALUES_LINK_SRC", "discovery/defaultValuesLinks/0/src"},
            // fty-discovery parameters
            {"FTY_DISCOVERY_DUMP_POOL", "parameters/maxDumpPoolNumber"},
            {"FTY_DISCOVERY_SCAN_POOL", "parameters/maxScanPoolNumber"},
            {"FTY_DISCOVERY_SCAN_TIMEOUT", "parameters/nutScannerTimeOut"},
            {"FTY_DISCOVERY_DUMP_LOOPTIME", "parameters/dumpDataLoopTime"},
            // fty-session
            {"FTY_SESSION_TIMEOUT_NO_ACTIVITY", "timeout/no_activity"},
            {"FTY_SESSION_TIMEOUT_LEASE", "timeout/lease_time"}};
        if (config_mapping.find(key) == config_mapping.end())
            return key.c_str();
        return config_mapping.at(key).c_str();
    }

    const char* get_path(const std::string& key)
    {
        if (key.find("language") == 0) {
            return "/etc/fty-email/fty-email.cfg";
        } else if (key.find("BIOS_SMTP_") == 0) {
            return "/etc/fty-email/fty-email.cfg";
        } else if (key.find("BIOS_METRIC_STORE_") == 0) {
            return "/etc/fty-metric-store/fty-metric-store.cfg";
        } else if (key.find("BIOS_NUT_") == 0) {
            return "/etc/fty-nut/fty-nut.cfg";
        } else if (key.find("FTY_DISCOVERY_") == 0) {
            return "/etc/fty-discovery/fty-discovery.cfg";
        } else if (key.find("FTY_SESSION_") == 0) {
            return "/etc/fty/fty-session.cfg";
        }

        // general config file
        return "/etc/default/fty.cfg";
    }

    // put new value to zconfig_t - if key has / inside, it create the
    // hierarchy automatically.
    //
    // If c_value is nullptr, it simply create hierarchy
    static zconfig_t* s_zconfig_put(zconfig_t* config, const std::string& key, const char* c_value)
    {
        std::vector<std::string> keys = fty::split(key, "/");

        zconfig_t* cfg = config;
        for (const std::string& k : keys) {
            if (!zconfig_locate(cfg, k.c_str()))
                cfg = zconfig_new(k.c_str(), cfg);
            else
                cfg = zconfig_locate(cfg, k.c_str());
        }

        if (c_value)
            zconfig_set_value(cfg, "%s", c_value);

        return cfg;
    }

    static void assert_key(const std::string& key)
    {
        static std::string key_format_string = "^[-._a-zA-Z0-9/]+$";
        static std::regex  key_format(key_format_string);
        if (!std::regex_match(key, key_format)) {
            std::string msg = "to satisfy format " + key_format_string;
            bios_throw("request-param-bad", key.c_str(), key.c_str(), msg.c_str())
        }
    }

    static void assert_value(const std::string& key, const std::string& value)
    {
        static std::string     value_format_string = "^[[:blank:][:alnum:][:punct:]]*$";
        static std::regex value_format(value_format_string);
        if (!std::regex_match(value, value_format)) {
            std::string msg2 = "to satisfy format " + value_format_string;
            bios_throw("request-param-bad", key.c_str(), value.c_str(), msg2.c_str())
        }
    }

    void roots_destroy(const std::map<std::string, zconfig_t*>& roots)
    {
        for (const auto& it : roots) {
            zconfig_t* cfg = it.second;
            zconfig_destroy(&cfg);
        }
    }

    void json2zpl(std::map<std::string, zconfig_t*>& roots, const cxxtools::SerializationInfo& si,
        std::lock_guard<std::mutex>& lock)
    {
        static const std::string slash{"/"};

        if (si.category() != cxxtools::SerializationInfo::Object)
            bios_throw("bad-request-document", "Root of json request document must be an object")

                for (const auto& it : si)
            {

                assert_key(it.name());

                // this is a support for legacy input document, please drop it
                bool legacy_format =
                    it.name() == "config" && it.category() == cxxtools::SerializationInfo::Category::Object;
                if (legacy_format) {
                    cxxtools::SerializationInfo fake_si;
                    cxxtools::SerializationInfo fake_value = si.getMember("config").getMember("value");
                    std::string                 name;
                    si.getMember("config").getMember("key").getValue(name);

                    if (fake_value.category() == cxxtools::SerializationInfo::Category::Value) {
                        std::string value;
                        fake_value.getValue(value);
                        fake_si.addMember(name) <<= value;
                    } else if (fake_value.category() == cxxtools::SerializationInfo::Category::Array) {
                        std::vector<std::string> values;
                        fake_value >>= values;
                        fake_si.addMember(name) <<= values;
                    } else {
                        std::string msg = "Value of " + name + " must be string or array of strings.";
                        bios_throw("bad-request-document", msg.c_str())
                    }
                    json2zpl(roots, fake_si, lock);
                    continue;
                }

                std::string key;
                key = it.name();

                std::string file_path = get_path(key);
                if (!file_path.empty()) { // ignore unknown keys
                    if (roots.count(file_path) == 0) {
                        zconfig_t* root = zconfig_load(file_path.c_str());
                        if (!root)
                            root = zconfig_new("root", nullptr);
                        if (!root)
                            bios_throw("internal-error", "zconfig_new () failed.") roots[file_path] = root;
                    }
                }

                zconfig_t* cfg = roots[file_path];

                if (it.category() == cxxtools::SerializationInfo::Category::Value) {
                    std::string value;
                    it.getValue(value);
                    s_zconfig_put(cfg, get_mapping(it.name()), value.c_str());
                } else if (it.category() == cxxtools::SerializationInfo::Category::Array) {
                    std::vector<std::string> values;
                    it >>= values;
                    size_t i = 0;

                    zconfig_t* array_root = zconfig_locate(cfg, get_mapping(it.name().c_str()));
                    if (array_root) {
                        zconfig_t* ind = zconfig_child(array_root);
                        while (i) {
                            zconfig_set_value(ind, nullptr);
                            ind = zconfig_next(ind);
                        }
                    }

                    for (const auto& value : values) {
                        assert_value(it.name(), value);
                        std::string name = get_mapping(it.name()) + slash + std::to_string(i);
                        s_zconfig_put(cfg, name.c_str(), value.c_str());
                        i++;
                    }
                } else {
                    std::string msg = "Value of " + it.name() + " must be string or array of strings.";
                    bios_throw("bad-request-document", msg.c_str())
                }
            }
    }

} // namespace config

namespace email {

    static char* s_getenv(const char* name, const char* dfl)
    {
        char* ret = getenv(name);
        if (!ret)
            return const_cast<char*>(dfl);
        return ret;
    }

    /*static std::string
    s_read_all (const char* filename) {
        int fd = open (filename, O_RDONLY);
        if (fd <= -1)
            return std::string {};
        std::string ret = shared::read_all (fd);
        close (fd);
        return ret;
    }*/

    void x_headers(zhash_t* headers)
    {
        zhash_insert(headers, "X-Eaton-IPC-image-version", s_getenv("OSIMAGE_BASENAME", "unknown"));
        zhash_insert(headers, "X-Eaton-IPC-hardware-catalog-number", s_getenv("HARDWARE_CATALOG_NUMBER", "unknown"));
        zhash_insert(headers, "X-Eaton-IPC-hardware-spec-revision", s_getenv("HARDWARE_SPEC_REVISION", "unknown"));
        zhash_insert(headers, "X-Eaton-IPC-hardware-serial-number", s_getenv("HARDWARE_SERIAL_NUMBER", "unknown"));
        zhash_insert(headers, "X-Eaton-IPC-hardware-uuid", s_getenv("HARDWARE_UUID", "unknown"));
    }
} // namespace email

} // namespace utils
