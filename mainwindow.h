#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QNetworkReply;
class QFile;
class QHttpMultiPart;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_browseButton_clicked();
    void on_uploadButton_clicked();
    void on_openReleaseButton_clicked();
    void on_clearLogButton_clicked();
    void on_refreshOwnersButton_clicked();
    void on_refreshReposButton_clicked();

private:
    enum class Stage
    {
        Idle,
        FetchRelease,
        CreateRelease,
        DeleteAsset,
        UploadAsset
    };

    enum class OwnerListStage
    {
        Idle,
        FetchCurrentUser,
        FetchUserOrganizations,
        FetchPublicOrganizations
    };

    void loadSettings();
    void saveSettings();
    bool validateInputs();
    void setBusy(bool busy);
    void logMessage(const QString &text);
    void logReplyError(const QString &prefix, QNetworkReply *reply);
    void startUploadFlow();

    QString currentGiteaUrl() const;
    QString currentOwner() const;
    QString currentRepo() const;
    QString baseApiRepoUrl() const;
    QString releasePageUrl() const;
    QString fileNameOnly() const;
    QString releaseTitleOrFallback() const;

    void requestReleaseByTag();
    void createRelease();
    void deleteExistingAssetIfNeeded();
    void uploadAsset();

    void handleReply(QNetworkReply *reply);
    void handleFetchReleaseReply(QNetworkReply *reply, const QByteArray &body);
    void handleCreateReleaseReply(QNetworkReply *reply, const QByteArray &body);
    void handleDeleteAssetReply(QNetworkReply *reply, const QByteArray &body);
    void handleUploadReply(QNetworkReply *reply, const QByteArray &body);
    void handleOwnerListReply(QNetworkReply *reply);
    void handleRepositoryListReply(QNetworkReply *reply);
    void requestOwnerList();
    void requestOwnerListPath(OwnerListStage stage, const QString &path);
    void requestRepositoryList(bool userEndpoint);
    void scheduleRepositoryRefresh();
    void updateOwnerList(const QStringList &owners);
    void updateRepositoryList(const QStringList &repositories, bool clearCurrentText);
    QStringList defaultOwners() const;
    QString ownerNameFromObject(const QJsonObject &object) const;

    bool parseReleaseObject(const QByteArray &body, QString *releaseIdOut, QString *existingAssetIdOut, QString *htmlUrlOut);
    QString extractBestErrorText(const QByteArray &body) const;
    QString trimTrailingSlash(const QString &s) const;

private:
    Ui::MainWindow *ui;
    QNetworkAccessManager m_net;
    QNetworkAccessManager m_ownerNet;
    QNetworkAccessManager m_repoNet;
    QTimer m_repoRefreshTimer;
    Stage m_stage;
    OwnerListStage m_ownerListStage;
    bool m_retryRepoListAsUser;
    bool m_clearRepoTextOnNextRefresh;
    bool m_clearRepoTextForCurrentRefresh;

    QString m_releaseId;
    QString m_existingAssetId;
    QString m_releaseHtmlUrl;
    QString m_lastRepositoryRefreshOwner;

    QPointer<QFile> m_uploadFile;
    QPointer<QHttpMultiPart> m_uploadMultiPart;
    QPointer<QNetworkReply> m_ownerListReply;
    QPointer<QNetworkReply> m_repoListReply;
    QStringList m_pendingOwners;
};

#endif // MAINWINDOW_H
