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

#pragma once
#include "Global.h"

#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>

class PageRecord;

class HTTPServer : public QObject {
    Q_OBJECT

private:
    QTcpServer* m_server;
    PageRecord* m_page_record;
    QMap<QTcpSocket*, QByteArray> m_request_buffers;

public:
    HTTPServer(PageRecord* page_record);
    ~HTTPServer();

    bool Start(int port);
    void Stop();

    static QJsonObject CreateSuccessResponse(const QJsonObject& data = QJsonObject());
    static QJsonObject CreateErrorResponse(const QString& message);

private slots:
    void OnNewConnection();
    void OnReadyRead();
    void OnDisconnected();

private:
    void HandleRequest(QTcpSocket* socket, const QByteArray& request);
    void HandleAPI(QTcpSocket* socket, const QString& path, const QJsonObject& json);
    void SendResponse(QTcpSocket* socket, int status, const QByteArray& content_type, const QByteArray& content);
    void SendJsonResponse(QTcpSocket* socket, int status, const QJsonObject& json);

    // API handlers
    QJsonObject HandleAPIStatus();
    QJsonObject HandleAPIStartRecording();
    QJsonObject HandleAPIPauseRecording();
    QJsonObject HandleAPICancelRecording();
    QJsonObject HandleAPISaveRecording();
}; 