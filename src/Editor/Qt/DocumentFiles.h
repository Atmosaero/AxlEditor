#pragma once
#include <QFile>
#include <QSaveFile>
#include <stdexcept>

namespace DocumentFiles {
inline QByteArray Read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    const auto bytes = file.readAll();
    if (file.error() != QFile::NoError) throw std::runtime_error(file.errorString().toStdString());
    return bytes;
}
inline void Write(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
}
