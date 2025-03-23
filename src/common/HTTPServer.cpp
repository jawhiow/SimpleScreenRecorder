/*
Copyright (c) 2012-2020 Maarten Baert <maarten-baert@hotmail.com>

This file is part of SimpleScreenRecorder.

SimpleScreenRecorder is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SimpleScreenRecorder is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SimpleScreenRecorder.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "HTTPServer.h"
#include "Logger.h"
#include "PageRecord.h"

HTTPServer::HTTPServer(PageRecord* page_record) {
    Logger::LogInfo("[HTTPServer::HTTPServer] " + Logger::tr("Creating HTTP server..."));
    
    if(page_record == NULL) {
        Logger::LogError("[HTTPServer::HTTPServer] " + Logger::tr("Error: page_record is NULL!"));
        throw std::runtime_error("PageRecord is NULL");
    }
    
    try {
        m_server = new QTcpServer(this);
        if(m_server == NULL) {
            Logger::LogError("[HTTPServer::HTTPServer] " + Logger::tr("Error: Could not create QTcpServer!"));
            throw std::runtime_error("Could not create QTcpServer");
        }
        
        m_page_record = page_record;
        Logger::LogInfo("[HTTPServer::HTTPServer] " + Logger::tr("Connecting signals..."));
        
        // 检查信号和槽连接是否成功
        bool connected = connect(m_server, SIGNAL(newConnection()), this, SLOT(OnNewConnection()));
        if(!connected) {
            Logger::LogError("[HTTPServer::HTTPServer] " + Logger::tr("Error: Could not connect newConnection signal!"));
            throw std::runtime_error("Could not connect signal");
        }
        
        Logger::LogInfo("[HTTPServer::HTTPServer] " + Logger::tr("HTTP server created successfully."));
    } catch (const std::exception& e) {
        Logger::LogError("[HTTPServer::HTTPServer] " + Logger::tr("Error during HTTP server creation: %1").arg(e.what()));
        throw;
    } catch (...) {
        Logger::LogError("[HTTPServer::HTTPServer] " + Logger::tr("Unknown error during HTTP server creation!"));
        throw;
    }
}

HTTPServer::~HTTPServer() {
    Stop();
    delete m_server;
}

bool HTTPServer::Start(int port) {
    if (!m_server->listen(QHostAddress::Any, port)) {
        Logger::LogError("[HTTPServer::Start] " + Logger::tr("Error: Could not start HTTP server on port %1!").arg(port));
        return false;
    }
    
    Logger::LogInfo("[HTTPServer::Start] " + Logger::tr("HTTP server listening on port %1.").arg(port));
    return true;
}

void HTTPServer::Stop() {
    if (m_server->isListening()) {
        m_server->close();
        Logger::LogInfo("[HTTPServer::Stop] " + Logger::tr("HTTP server stopped."));
    }
}

QJsonObject HTTPServer::CreateSuccessResponse(const QJsonObject& data) {
    QJsonObject response;
    response["success"] = true;
    response["data"] = data;
    return response;
}

QJsonObject HTTPServer::CreateErrorResponse(const QString& message) {
    QJsonObject response;
    response["success"] = false;
    response["error"] = message;
    return response;
}

void HTTPServer::OnNewConnection() {
    QTcpSocket* socket = m_server->nextPendingConnection();
    connect(socket, SIGNAL(readyRead()), this, SLOT(OnReadyRead()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(OnDisconnected()));
    m_request_buffers[socket] = QByteArray();
}

void HTTPServer::OnReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        Logger::LogError("[HTTPServer::OnReadyRead] " + Logger::tr("Error: Invalid socket!"));
        return;
    }
    
    // 确保socket仍在map中
    if (!m_request_buffers.contains(socket)) {
        Logger::LogWarning("[HTTPServer::OnReadyRead] " + Logger::tr("Warning: Socket not found in buffer map."));
        return;
    }
    
    try {
        QByteArray data = socket->readAll();
        if (data.isEmpty()) {
            Logger::LogWarning("[HTTPServer::OnReadyRead] " + Logger::tr("Warning: Empty data received."));
            return;
        }
        
        QByteArray& buffer = m_request_buffers[socket];
        buffer.append(data);
        
        // 检查是否有完整的HTTP请求
        if (buffer.contains("\r\n\r\n")) {
            QByteArray requestCopy = buffer;  // 创建一个副本以便安全处理
            HandleRequest(socket, requestCopy);
            
            // 安全地清除缓冲区
            if (m_request_buffers.contains(socket)) {
                m_request_buffers[socket].clear();
            }
        }
    } catch (const std::exception& e) {
        Logger::LogError("[HTTPServer::OnReadyRead] " + Logger::tr("Error processing request: %1").arg(e.what()));
        if (socket && socket->isOpen()) {
            SendResponse(socket, 500, "text/plain", "Internal Server Error");
        }
    } catch (...) {
        Logger::LogError("[HTTPServer::OnReadyRead] " + Logger::tr("Unknown error processing request!"));
        if (socket && socket->isOpen()) {
            SendResponse(socket, 500, "text/plain", "Internal Server Error");
        }
    }
}

void HTTPServer::OnDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        Logger::LogWarning("[HTTPServer::OnDisconnected] " + Logger::tr("Warning: Invalid socket in disconnect handler."));
        return;
    }
    
    try {
        // 安全地从映射中删除套接字
        if (m_request_buffers.contains(socket)) {
            m_request_buffers.remove(socket);
            Logger::LogInfo("[HTTPServer::OnDisconnected] " + Logger::tr("Client disconnected, cleaned up resources."));
        }
        
        socket->deleteLater();
    } catch (const std::exception& e) {
        Logger::LogError("[HTTPServer::OnDisconnected] " + Logger::tr("Error during disconnect handling: %1").arg(e.what()));
    } catch (...) {
        Logger::LogError("[HTTPServer::OnDisconnected] " + Logger::tr("Unknown error during disconnect handling!"));
    }
}

void HTTPServer::HandleRequest(QTcpSocket* socket, const QByteArray& request) {
    if (!socket || !socket->isOpen()) {
        Logger::LogError("[HTTPServer::HandleRequest] " + Logger::tr("Error: Invalid or closed socket!"));
        return;
    }
    
    try {
        // 检查请求是否为空
        if (request.isEmpty()) {
            Logger::LogWarning("[HTTPServer::HandleRequest] " + Logger::tr("Warning: Empty request received."));
            SendResponse(socket, 400, "text/plain", "Bad Request");
            return;
        }
        
        QStringList lines = QString(request).split("\r\n");
        if (lines.isEmpty()) {
            Logger::LogWarning("[HTTPServer::HandleRequest] " + Logger::tr("Warning: Request has no lines."));
            SendResponse(socket, 400, "text/plain", "Bad Request");
            return;
        }
        
        QStringList requestLine = lines[0].split(" ");
        if (requestLine.size() < 3) {
            Logger::LogWarning("[HTTPServer::HandleRequest] " + Logger::tr("Warning: Invalid request line: %1").arg(lines[0]));
            SendResponse(socket, 400, "text/plain", "Bad Request");
            return;
        }
        
        QString method = requestLine[0];
        QString path = requestLine[1];
        
        Logger::LogInfo("[HTTPServer::HandleRequest] " + Logger::tr("Received %1 request for %2").arg(method).arg(path));
        
        // Parse headers
        QMap<QString, QString> headers;
        for (int i = 1; i < lines.size(); ++i) {
            if (lines[i].isEmpty())
                break;
            
            int colonPos = lines[i].indexOf(':');
            if (colonPos > 0) {
                QString key = lines[i].left(colonPos).trimmed().toLower();
                QString value = lines[i].mid(colonPos + 1).trimmed();
                headers[key] = value;
            }
        }
        
        // Find the body
        QByteArray body;
        int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd >= 0 && headerEnd + 4 < request.size()) {
            body = request.mid(headerEnd + 4);
        }
        
        // 处理API路径 
        // 简化API路径处理，移除前导斜杠以保持一致性
        if (path.startsWith("/")) {
            path = path.mid(1); // 去掉前导斜杠
        }
        
        // 处理常见的API终端点
        if (path == "start" || path == "record/start") {
            QJsonObject response = HandleAPIStartRecording();
            SendJsonResponse(socket, 200, response);
            return;
        } else if (path == "pause" || path == "record/pause") {
            QJsonObject response = HandleAPIPauseRecording();
            SendJsonResponse(socket, 200, response);
            return;
        } else if (path == "save" || path == "record/save") {
            QJsonObject response = HandleAPISaveRecording();
            SendJsonResponse(socket, 200, response);
            return;
        } else if (path == "cancel" || path == "record/cancel") {
            QJsonObject response = HandleAPICancelRecording();
            SendJsonResponse(socket, 200, response);
            return;
        } else if (path == "status" || path == "record/status") {
            QJsonObject response = HandleAPIStatus();
            SendJsonResponse(socket, 200, response);
            return;
        } else if (path == "api/status" || path == "api/record/status") {
            QJsonObject response = HandleAPIStatus();
            SendJsonResponse(socket, 200, response);
            return;
        } else if (path == "" || path == "index.html" || path == "index") {
            // 处理根路径或索引请求
            QByteArray content = "SimpleScreenRecorder API Server\n\n"
                               "Available endpoints:\n"
                               "- /start - Start recording\n"
                               "- /pause - Pause recording\n"
                               "- /save - Save recording\n"
                               "- /cancel - Cancel recording\n"
                               "- /status - Get status information\n";
            SendResponse(socket, 200, "text/plain", content);
            return;
        }
        
        // 处理JSON API请求 (保留原有逻辑以向后兼容)
        if (path.startsWith("api/")) {
            QString apiPath = path.mid(4);
            QJsonObject jsonRequest;
            
            if (!body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (!doc.isNull() && doc.isObject()) {
                    jsonRequest = doc.object();
                }
            }
            
            HandleAPI(socket, apiPath, jsonRequest);
            return;
        }
        
        // 未找到有效的API端点
        Logger::LogWarning("[HTTPServer::HandleRequest] " + Logger::tr("Unknown path: %1").arg(path));
        SendResponse(socket, 404, "text/plain", "Not Found");
    } catch (const std::exception& e) {
        Logger::LogError("[HTTPServer::HandleRequest] " + Logger::tr("Error handling request: %1").arg(e.what()));
        if (socket && socket->isOpen()) {
            SendResponse(socket, 500, "text/plain", "Internal Server Error");
        }
    } catch (...) {
        Logger::LogError("[HTTPServer::HandleRequest] " + Logger::tr("Unknown error handling request!"));
        if (socket && socket->isOpen()) {
            SendResponse(socket, 500, "text/plain", "Internal Server Error");
        }
    }
}

void HTTPServer::HandleAPI(QTcpSocket* socket, const QString& path, const QJsonObject& json) {
    QJsonObject response;
    
    if (path == "status") {
        response = HandleAPIStatus();
    } else if (path == "record/start") {
        response = HandleAPIStartRecording();
    } else if (path == "record/pause") {
        response = HandleAPIPauseRecording();
    } else if (path == "record/cancel") {
        response = HandleAPICancelRecording();
    } else if (path == "record/save") {
        response = HandleAPISaveRecording();
    } else {
        response = CreateErrorResponse("Unknown API endpoint");
    }
    
    SendJsonResponse(socket, 200, response);
}

void HTTPServer::SendResponse(QTcpSocket* socket, int status, const QByteArray& content_type, const QByteArray& content) {
    QByteArray response;
    
    // Status line
    QString statusText;
    switch (status) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        default: statusText = "Unknown";
    }
    
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(status).arg(statusText).toUtf8());
    
    // Headers
    response.append(QString("Content-Type: %1\r\n").arg(QString(content_type)).toUtf8());
    response.append(QString("Content-Length: %1\r\n").arg(content.size()).toUtf8());
    response.append("Connection: close\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    
    // Empty line + body
    response.append("\r\n");
    response.append(content);
    
    socket->write(response);
    socket->flush();
    socket->close();
}

void HTTPServer::SendJsonResponse(QTcpSocket* socket, int status, const QJsonObject& json) {
    QJsonDocument doc(json);
    QByteArray content = doc.toJson();
    SendResponse(socket, status, "application/json", content);
}

QJsonObject HTTPServer::HandleAPIStatus() {
    QJsonObject data;
    
    // Get information about the current state
    if (m_page_record) {
        // Convert information from PageRecord to JSON format
        // These values will need to be exposed from PageRecord to be accessible here
        data["is_recording"] = m_page_record->IsRecording();
        data["is_paused"] = m_page_record->IsPaused();
        data["file_name"] = m_page_record->GetCurrentFileName();
        data["file_size"] = QString::number(m_page_record->GetCurrentFileSize());
        data["total_time"] = m_page_record->GetTotalTime();
    }
    
    return CreateSuccessResponse(data);
}

QJsonObject HTTPServer::HandleAPIStartRecording() {
    if (!m_page_record)
        return CreateErrorResponse("No access to recording page");
    
    // If paused, unpause, else start recording
    if (m_page_record->IsPaused()) {
        m_page_record->OnRecordStartPause();
        return CreateSuccessResponse({{"action", "resumed"}});
    } else if (!m_page_record->IsRecording()) {
        m_page_record->OnRecordStart();
        return CreateSuccessResponse({{"action", "started"}});
    } else {
        return CreateErrorResponse("Already recording");
    }
}

QJsonObject HTTPServer::HandleAPIPauseRecording() {
    if (!m_page_record)
        return CreateErrorResponse("No access to recording page");
    
    // Can only pause if currently recording and not already paused
    if (m_page_record->IsRecording() && !m_page_record->IsPaused()) {
        m_page_record->OnRecordPause();
        return CreateSuccessResponse();
    } else {
        return CreateErrorResponse("Not recording or already paused");
    }
}

QJsonObject HTTPServer::HandleAPICancelRecording() {
    if (!m_page_record)
        return CreateErrorResponse("No access to recording page");
    
    // Can only cancel if currently recording or paused
    if (m_page_record->IsRecording()) {
        m_page_record->OnRecordCancel(false); // false = no confirmation dialog
        return CreateSuccessResponse();
    } else {
        return CreateErrorResponse("Not recording");
    }
}

QJsonObject HTTPServer::HandleAPISaveRecording() {
    if (!m_page_record)
        return CreateErrorResponse("No access to recording page");
    
    // Can only save if currently recording or paused
    if (m_page_record->IsRecording()) {
        m_page_record->OnRecordSave(false); // false = no confirmation dialog
        return CreateSuccessResponse();
    } else {
        return CreateErrorResponse("Not recording");
    }
} 