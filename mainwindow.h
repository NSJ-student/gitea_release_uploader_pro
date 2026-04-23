#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QPointer>

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

private:
    enum class Stage
    {
        Idle,
        FetchRelease,
        CreateRelease,
        DeleteAsset,
        UploadAsset
    };

    void loadSettings();
    void saveSettings();
    bool validateInputs();
    void setBusy(bool busy);
    void logMessage(const QString &text);
    void logReplyError(const QString &prefix, QNetworkReply *reply);
    void startUploadFlow();

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

    bool parseReleaseObject(const QByteArray &body, QString *releaseIdOut, QString *existingAssetIdOut, QString *htmlUrlOut);
    QString extractBestErrorText(const QByteArray &body) const;
    QString trimTrailingSlash(const QString &s) const;

private:
    Ui::MainWindow *ui;
    QNetworkAccessManager m_net;
    Stage m_stage;

    QString m_releaseId;
    QString m_existingAssetId;
    QString m_releaseHtmlUrl;

    QPointer<QFile> m_uploadFile;
    QPointer<QHttpMultiPart> m_uploadMultiPart;
};

#endif // MAINWINDOW_H
