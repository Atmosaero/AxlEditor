#include "Reference/Assets/ReferenceAssetProvider.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <set>

namespace {
bool WriteFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("asset") == 5;
}

std::optional<AssetInfo> FindPath(const ReferenceAssetProvider& provider, const QString& path)
{
    const auto source = QFileInfo(path).absoluteFilePath().toUtf8().toStdString();
    for (const auto& asset : provider.GetAssets())
        if (asset.sourcePath == source) return asset;
    return std::nullopt;
}
}

class AssetProviderTests final : public QObject
{
    Q_OBJECT
private slots:
    void recursiveScanKeepsUtf8MetadataAndTypes()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto directory = temp.path() + "/" + QString::fromUtf8(u8"\u0410\u0441\u0441\u0435\u0442\u044b");
        QVERIFY(QDir().mkpath(directory + "/nested"));
        const std::vector<std::pair<QString, std::string>> files{
            {QString::fromUtf8(u8"\u0442\u0435\u043a\u0441\u0442\u0443\u0440\u0430.PNG"), "Texture"},
            {"photo.jpg", "Texture"}, {"cube.obj", "Mesh"}, {"model.GLB", "Mesh"},
            {"level.axl", "Scene"}, {"scene.JSON", "Scene"}, {"code.LUA", "Script"},
            {"other.xyz", "Unknown"}, {"README", "Unknown"}, {"nested/cube.obj", "Mesh"}};
        for (const auto& [name, type] : files) QVERIFY(WriteFile(directory + "/" + name));

        ReferenceAssetProvider provider(directory + "/./nested/..");
        provider.Refresh();
        QCOMPARE(provider.GetAssets().size(), files.size());
        std::set<AssetId> ids;
        for (const auto& [name, type] : files) {
            const auto asset = FindPath(provider, directory + "/" + name);
            QVERIFY(asset.has_value());
            QVERIFY(asset->id != 0);
            QVERIFY(ids.insert(asset->id).second);
            QCOMPARE(asset->name, QFileInfo(name).fileName().toUtf8().toStdString());
            QCOMPARE(asset->type, type);
            QVERIFY(QFileInfo(QString::fromStdString(asset->sourcePath)).isAbsolute());
            QCOMPARE(provider.GetAsset(asset->id)->sourcePath, asset->sourcePath);
        }
        QVERIFY(!provider.GetAsset(0));
        QVERIFY(!provider.GetAsset(AssetId{1} << 60));
    }

    void refreshPreservesIdsAndRemovesMissingFiles()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto first = temp.path() + "/middle.png";
        const auto second = temp.path() + "/last.lua";
        QVERIFY(WriteFile(first));
        QVERIFY(WriteFile(second));
        ReferenceAssetProvider provider(temp.path());
        provider.Refresh();
        QVERIFY(FindPath(provider, first));
        QVERIFY(FindPath(provider, second));
        const auto firstId = FindPath(provider, first)->id;
        const auto secondId = FindPath(provider, second)->id;
        QVERIFY(WriteFile(temp.path() + "/aaa.json")); // Insertion changes sorted row indices.
        provider.Refresh();
        QVERIFY(FindPath(provider, first));
        QVERIFY(FindPath(provider, second));
        QCOMPARE(FindPath(provider, first)->id, firstId);
        QCOMPARE(FindPath(provider, second)->id, secondId);
        QCOMPARE(provider.GetAssets().size(), size_t{3});

        auto snapshot = provider.GetAssets();
        snapshot.front().name = "caller changed a copy";
        QVERIFY(provider.GetAssets().front().name != snapshot.front().name);
        QVERIFY(QFile::remove(first));
        provider.Refresh();
        QVERIFY(!provider.GetAsset(firstId));
        QVERIFY(provider.GetAsset(secondId));
        QCOMPARE(provider.GetAsset(secondId)->type, std::string("Script"));
        QVERIFY(WriteFile(first));
        provider.Refresh();
        QVERIFY(FindPath(provider, first));
        QCOMPARE(FindPath(provider, first)->id, firstId); // Reference IDs are tied to paths for this session.
    }

    void missingDirectoryCanAppearAndDisappear()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto directory = temp.path() + "/Assets";
        ReferenceAssetProvider provider(directory);
        provider.Refresh();
        QVERIFY(provider.GetAssets().empty());
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(WriteFile(directory + "/unknown.data"));
        provider.Refresh();
        QCOMPARE(provider.GetAssets().size(), size_t{1});
        const auto id = provider.GetAssets().front().id;
        QVERIFY(QFile::remove(directory + "/unknown.data"));
        QVERIFY(QDir().rmdir(directory));
        provider.Refresh();
        QVERIFY(provider.GetAssets().empty());
        QVERIFY(!provider.GetAsset(id));
    }
};

QTEST_APPLESS_MAIN(AssetProviderTests)
#include "AssetProviderTests.moc"
