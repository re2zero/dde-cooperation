// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "secureconfig.h"

        #include "sslcertconf.h"

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

namespace {

std::mutex g_configMutex;
std::string g_configDir;

std::string configDir()
{
    std::lock_guard<std::mutex> lock(g_configMutex);
    if (!g_configDir.empty())
        return g_configDir;

    // Fallback for standalone native runs/tests. The app always provides its
    // private files directory through JNI.
    if (const char *home = std::getenv("HOME"))
        return std::string(home) + "/.cooperation";
    return (std::filesystem::temp_directory_path() / "dde-cooperation").string();
}

std::string getCertFilePath()
{
    const std::string dir = configDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    SslCertConf::ins()->generateCertificate(dir);
    return SslCertConf::ins()->getCertificatePath();
}

} // namespace

void SecureConfig::setConfigDir(const std::string &dir)
{
    std::lock_guard<std::mutex> lock(g_configMutex);
    g_configDir = dir;
}

std::shared_ptr<NetUtil::Asio::SSLContext> SecureConfig::serverContext()
{
    const std::string certfile = getCertFilePath();

    auto context = std::make_shared<NetUtil::Asio::SSLContext>(asio::ssl::context::tlsv13);
    context->use_certificate_file(certfile, asio::ssl::context::pem);
    context->use_rsa_private_key_file(certfile, asio::ssl::context::pem);

    return context;
}

std::shared_ptr<NetUtil::Asio::SSLContext> SecureConfig::clientContext()
{
    const std::string certfile = getCertFilePath();

    auto context = std::make_shared<NetUtil::Asio::SSLContext>(asio::ssl::context::tlsv13);
    context->use_certificate_file(certfile, asio::ssl::context::pem);

    return context;
}
