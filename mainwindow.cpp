#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_stage(Stage::Idle)
{
    ui->setupUi(this);

    ui->tokenEdit->setEchoMode(QLineEdit::Password);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->openReleaseButton->setEnabled(false);

    connect(&m_net, &QNetworkAccessManager::finished,
            this, &MainWindow::handleReply);

    loadSettings();
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::loadSettings()
{
    QSettings s("OpenAI", "GiteaReleaseUploaderPro");
    ui->urlEdit->setText(s.value("url", "http://localhost:3000").toString());
    ui->ownerEdit->setText(s.value("owner").toString());
    ui->repoEdit->setText(s.value("repo").toString());
    ui->tagEdit->setText(s.value("tag").toString());
    ui->titleEdit->setText(s.value("title").toString());
    ui->fileEdit->setText(s.value("file").toString());
    ui->tokenEdit->setText(s.value("token").toString());
    ui->replaceAssetCheck->setChecked(s.value("replaceAsset", true).toBool());
    ui->autoCreateReleaseCheck->setChecked(s.value("autoCreateRelease", true).toBool());
    ui->openBrowserOnSuccessCheck->setChecked(s.value("openBrowserOnSuccess", false).toBool());
}

void MainWindow::saveSettings()
{
    QSettings s("OpenAI", "GiteaReleaseUploaderPro");
    s.setValue("url", ui->urlEdit->text().trimmed());
    s.setValue("owner", ui->ownerEdit->text().trimmed());
    s.setValue("repo", ui->repoEdit->text().trimmed());
    s.setValue("tag", ui->tagEdit->text().trimmed());
    s.setValue("title", ui->titleEdit->text().trimmed());
    s.setValue("file", ui->fileEdit->text().trimmed());
    s.setValue("token", ui->tokenEdit->text().trimmed());
    s.setValue("replaceAsset", ui->replaceAssetCheck->isChecked());
    s.setValue("autoCreateRelease", ui->autoCreateReleaseCheck->isChecked());
    s.setValue("openBrowserOnSuccess", ui->openBrowserOnSuccessCheck->isChecked());
}

QString MainWindow::trimTrailingSlash(const QString &s) const
{
    QString out = s.trimmed();
    while (out.endsWith('/'))
        out.chop(1);
    return out;
}

QString MainWindow::baseApiRepoUrl() const
{
    return trimTrailingSlash(ui->urlEdit->text()) +
           "/api/v1/repos/" +
           ui->ownerEdit->text().trimmed() + "/" +
           ui->repoEdit->text().trimmed();
}

QString MainWindow::releasePageUrl() const
{
    return trimTrailingSlash(ui->urlEdit->text()) + "/" +
           ui->ownerEdit->text().trimmed() + "/" +
           ui->repoEdit->text().trimmed() + "/releases/tag/" +
           ui->tagEdit->text().trimmed();
}

QString MainWindow::fileNameOnly() const
{
    return QFileInfo(ui->fileEdit->text().trimmed()).fileName();
}

QString MainWindow::releaseTitleOrFallback() const
{
    const QString title = ui->titleEdit->text().trimmed();
    if (!title.isEmpty())
        return title;
    return ui->tagEdit->text().trimmed();
}

bool MainWindow::validateInputs()
{
    const QString url = trimTrailingSlash(ui->urlEdit->text());
    const QString owner = ui->ownerEdit->text().trimmed();
    const QString repo = ui->repoEdit->text().trimmed();
    const QString tag = ui->tagEdit->text().trimmed();
    const QString token = ui->tokenEdit->text().trimmed();
    const QString file = ui->fileEdit->text().trimmed();

    if (url.isEmpty() || owner.isEmpty() || repo.isEmpty() || tag.isEmpty() || token.isEmpty() || file.isEmpty()) {
        QMessageBox::warning(this, "입력 오류", "URL, Token, Owner, Repo, Tag, 파일 경로는 필수입니다.");
        return false;
    }

    if (!QFileInfo::exists(file) || !QFileInfo(file).isFile()) {
        QMessageBox::warning(this, "파일 오류", "업로드할 파일이 존재하지 않습니다.");
        return false;
    }

    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        QMessageBox::warning(this, "URL 오류", "Gitea URL 형식이 올바르지 않습니다. 예: http://localhost:3000");
        return false;
    }

    return true;
}

void MainWindow::setBusy(bool busy)
{
    ui->uploadButton->setEnabled(!busy);
    ui->browseButton->setEnabled(!busy);
    ui->openReleaseButton->setEnabled(!busy && !m_releaseHtmlUrl.isEmpty());
    ui->urlEdit->setEnabled(!busy);
    ui->tokenEdit->setEnabled(!busy);
    ui->ownerEdit->setEnabled(!busy);
    ui->repoEdit->setEnabled(!busy);
    ui->tagEdit->setEnabled(!busy);
    ui->titleEdit->setEnabled(!busy);
    ui->fileEdit->setEnabled(!busy);
    ui->replaceAssetCheck->setEnabled(!busy);
    ui->autoCreateReleaseCheck->setEnabled(!busy);
    ui->openBrowserOnSuccessCheck->setEnabled(!busy);
}

void MainWindow::logMessage(const QString &text)
{
    ui->logEdit->appendPlainText(text);
}

QString MainWindow::extractBestErrorText(const QByteArray &body) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.contains("message"))
            return obj.value("message").toString();
        if (obj.contains("error"))
            return obj.value("error").toString();
        if (obj.contains("url"))
            return obj.value("url").toString();
    }
    return QString::fromUtf8(body).trimmed();
}

void MainWindow::logReplyError(const QString &prefix, QNetworkReply *reply)
{
    QString text = prefix + ": " + reply->errorString();
    const QString detail = extractBestErrorText(reply->readAll());
    if (!detail.isEmpty())
        text += " / " + detail;
    logMessage(text);
}

void MainWindow::on_browseButton_clicked()
{
    const QString file = QFileDialog::getOpenFileName(this, "업로드할 파일 선택", ui->fileEdit->text());
    if (!file.isEmpty())
        ui->fileEdit->setText(file);
}

void MainWindow::on_clearLogButton_clicked()
{
    ui->logEdit->clear();
}

void MainWindow::on_openReleaseButton_clicked()
{
    const QString url = !m_releaseHtmlUrl.isEmpty() ? m_releaseHtmlUrl : releasePageUrl();
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::on_uploadButton_clicked()
{
    if (!validateInputs())
        return;

    saveSettings();
    ui->logEdit->clear();
    ui->progressBar->setValue(0);
    m_releaseId.clear();
    m_existingAssetId.clear();
    m_releaseHtmlUrl.clear();
    ui->openReleaseButton->setEnabled(false);

    startUploadFlow();
}

void MainWindow::startUploadFlow()
{
    logMessage("작업 시작");
    setBusy(true);
    requestReleaseByTag();
}

void MainWindow::requestReleaseByTag()
{
    m_stage = Stage::FetchRelease;
    const QString url = baseApiRepoUrl() + "/releases/tags/" + ui->tagEdit->text().trimmed();

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("token " + ui->tokenEdit->text().trimmed()).toUtf8());

    logMessage("Release 조회: " + url);
    m_net.get(req);
}

void MainWindow::createRelease()
{
    m_stage = Stage::CreateRelease;

    const QString url = baseApiRepoUrl() + "/releases";
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("token " + ui->tokenEdit->text().trimmed()).toUtf8());

    QJsonObject obj;
    obj["tag_name"] = ui->tagEdit->text().trimmed();
    obj["name"] = releaseTitleOrFallback();
    obj["body"] = ui->notesEdit->toPlainText();
    obj["draft"] = false;
    obj["prerelease"] = false;
    obj["target_commitish"] = "";

    logMessage("Release가 없어 자동 생성합니다.");
    m_net.post(req, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void MainWindow::deleteExistingAssetIfNeeded()
{
    if (!ui->replaceAssetCheck->isChecked() || m_existingAssetId.isEmpty()) {
        uploadAsset();
        return;
    }

    m_stage = Stage::DeleteAsset;
    const QString url = baseApiRepoUrl() + "/releases/" + m_releaseId + "/assets/" + m_existingAssetId;

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Authorization", ("token " + ui->tokenEdit->text().trimmed()).toUtf8());

    logMessage("동일 이름 asset 삭제 후 재업로드합니다.");
    m_net.deleteResource(req);
}

void MainWindow::uploadAsset()
{
    m_stage = Stage::UploadAsset;

    QFile *file = new QFile(ui->fileEdit->text().trimmed());
    if (!file->open(QIODevice::ReadOnly)) {
        logMessage("파일 열기 실패: " + file->errorString());
        file->deleteLater();
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    m_uploadFile = file;

    QUrl url(baseApiRepoUrl() + "/releases/" + m_releaseId + "/assets");
    QUrlQuery query;
    query.addQueryItem("name", fileNameOnly());
    url.setQuery(query);

    QNetworkRequest req{url};
    req.setRawHeader("Authorization", ("token " + ui->tokenEdit->text().trimmed()).toUtf8());

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"attachment\"; filename=\"" + fileNameOnly() + "\""));
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(filePart);

    m_uploadMultiPart = multiPart;

    logMessage("Asset 업로드 시작: " + fileNameOnly());
    QNetworkReply *reply = m_net.post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
        if (total > 0) {
            const int percent = int((double(sent) / double(total)) * 100.0);
            ui->progressBar->setValue(percent);
        }
    });
}

bool MainWindow::parseReleaseObject(const QByteArray &body, QString *releaseIdOut, QString *existingAssetIdOut, QString *htmlUrlOut)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return false;

    const QJsonObject obj = doc.object();

    const QJsonValue idVal = obj.value("id");
    if (releaseIdOut)
        *releaseIdOut = QString::number(idVal.toVariant().toLongLong());

    if (htmlUrlOut) {
        const QString html = obj.value("html_url").toString();
        if (!html.isEmpty())
            *htmlUrlOut = html;
    }

    if (existingAssetIdOut) {
        existingAssetIdOut->clear();
        const QJsonArray assets = obj.value("assets").toArray();
        for (const QJsonValue &v : assets) {
            const QJsonObject a = v.toObject();
            if (a.value("name").toString() == fileNameOnly()) {
                *existingAssetIdOut = QString::number(a.value("id").toVariant().toLongLong());
                break;
            }
        }
    }

    return !releaseIdOut->isEmpty();
}

void MainWindow::handleReply(QNetworkReply *reply)
{
    const QByteArray body = reply->readAll();

    switch (m_stage) {
    case Stage::FetchRelease:
        handleFetchReleaseReply(reply, body);
        break;
    case Stage::CreateRelease:
        handleCreateReleaseReply(reply, body);
        break;
    case Stage::DeleteAsset:
        handleDeleteAssetReply(reply, body);
        break;
    case Stage::UploadAsset:
        handleUploadReply(reply, body);
        break;
    default:
        break;
    }

    reply->deleteLater();
}

void MainWindow::handleFetchReleaseReply(QNetworkReply *reply, const QByteArray &body)
{
    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404 && ui->autoCreateReleaseCheck->isChecked()) {
            logMessage("Release를 찾지 못했습니다. 자동 생성으로 진행합니다.");
            createRelease();
            return;
        }

        QString msg = "Release 조회 실패: " + reply->errorString();
        const QString detail = extractBestErrorText(body);
        if (!detail.isEmpty())
            msg += " / " + detail;
        logMessage(msg);
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    if (!parseReleaseObject(body, &m_releaseId, &m_existingAssetId, &m_releaseHtmlUrl)) {
        logMessage("Release 응답 파싱 실패");
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    logMessage("Release 조회 성공. ID=" + m_releaseId);
    if (!m_existingAssetId.isEmpty())
        logMessage("같은 이름의 기존 asset을 찾았습니다.");

    deleteExistingAssetIfNeeded();
}

void MainWindow::handleCreateReleaseReply(QNetworkReply *reply, const QByteArray &body)
{
    if (reply->error() != QNetworkReply::NoError) {
        QString msg = "Release 생성 실패: " + reply->errorString();
        const QString detail = extractBestErrorText(body);
        if (!detail.isEmpty())
            msg += " / " + detail;
        logMessage(msg);
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    if (!parseReleaseObject(body, &m_releaseId, &m_existingAssetId, &m_releaseHtmlUrl)) {
        logMessage("생성된 Release 응답 파싱 실패");
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    logMessage("Release 생성 성공. ID=" + m_releaseId);
    deleteExistingAssetIfNeeded();
}

void MainWindow::handleDeleteAssetReply(QNetworkReply *reply, const QByteArray &body)
{
    Q_UNUSED(body)

    if (reply->error() != QNetworkReply::NoError) {
        QString msg = "기존 asset 삭제 실패: " + reply->errorString();
        const QString detail = extractBestErrorText(body);
        if (!detail.isEmpty())
            msg += " / " + detail;
        logMessage(msg);
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    logMessage("기존 asset 삭제 완료");
    uploadAsset();
}

void MainWindow::handleUploadReply(QNetworkReply *reply, const QByteArray &body)
{
    if (reply->error() != QNetworkReply::NoError) {
        QString msg = "업로드 실패: " + reply->errorString();
        const QString detail = extractBestErrorText(body);
        if (!detail.isEmpty())
            msg += " / " + detail;
        logMessage(msg);
        ui->progressBar->setValue(0);
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    ui->progressBar->setValue(100);

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QString browserUrl = doc.object().value("browser_download_url").toString();
        if (!browserUrl.isEmpty())
            logMessage("다운로드 URL: " + browserUrl);
    }

    logMessage("업로드 성공");
    if (m_releaseHtmlUrl.isEmpty())
        m_releaseHtmlUrl = releasePageUrl();

    ui->openReleaseButton->setEnabled(true);
    setBusy(false);
    m_stage = Stage::Idle;

    if (ui->openBrowserOnSuccessCheck->isChecked())
        QDesktopServices::openUrl(QUrl(m_releaseHtmlUrl));
}
