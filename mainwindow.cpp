#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QComboBox>
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
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_repoRefreshTimer(this),
      m_tagRefreshTimer(this),
      m_stage(Stage::Idle),
      m_ownerListStage(OwnerListStage::Idle),
      m_retryRepoListAsUser(false),
      m_clearRepoTextOnNextRefresh(false),
      m_clearRepoTextForCurrentRefresh(false),
      m_clearTagTextOnNextRefresh(false),
      m_clearTagTextForCurrentRefresh(false)
{
    ui->setupUi(this);

    ui->tokenEdit->setEchoMode(QLineEdit::Password);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->openReleaseButton->setEnabled(false);
    m_repoRefreshTimer.setSingleShot(true);
    m_tagRefreshTimer.setSingleShot(true);

    connect(&m_net, &QNetworkAccessManager::finished,
            this, &MainWindow::handleReply);
    connect(&m_repoRefreshTimer, &QTimer::timeout,
            this, [this]() { requestRepositoryList(false); });
    connect(&m_tagRefreshTimer, &QTimer::timeout,
            this, &MainWindow::requestTagList);
    connect(ui->ownerEdit, &QComboBox::currentTextChanged,
            this, [this]() { scheduleRepositoryRefresh(); });
    connect(ui->urlEdit, &QComboBox::currentTextChanged,
            this, [this]() { scheduleRepositoryRefresh(); });
    connect(ui->repoEdit, &QComboBox::currentTextChanged,
            this, [this]() { scheduleTagRefresh(); });

    loadSettings();
    m_lastRepositoryRefreshOwner = currentOwner();
    m_lastTagRefreshRepoKey = currentOwner() + "/" + currentRepo();
    scheduleRepositoryRefresh();
    scheduleTagRefresh();
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::loadSettings()
{
    QSettings s("OpenAI", "GiteaReleaseUploaderPro");
    ui->urlEdit->setEditText(s.value("url", "http://192.168.10.144:3020").toString());
    ui->ownerEdit->setEditText(s.value("owner", ui->ownerEdit->currentText()).toString());
    ui->repoEdit->setEditText(s.value("repo").toString());
    ui->tagEdit->setEditText(s.value("tag").toString());
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
    s.setValue("url", ui->urlEdit->currentText().trimmed());
    s.setValue("owner", ui->ownerEdit->currentText().trimmed());
    s.setValue("repo", ui->repoEdit->currentText().trimmed());
    s.setValue("tag", ui->tagEdit->currentText().trimmed());
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

QString MainWindow::currentGiteaUrl() const
{
    return trimTrailingSlash(ui->urlEdit->currentText());
}

QString MainWindow::currentOwner() const
{
    return ui->ownerEdit->currentText().trimmed();
}

QString MainWindow::currentRepo() const
{
    return ui->repoEdit->currentText().trimmed();
}

QString MainWindow::currentTag() const
{
    return ui->tagEdit->currentText().trimmed();
}

QString MainWindow::baseApiRepoUrl() const
{
    return currentGiteaUrl() +
           "/api/v1/repos/" +
           currentOwner() + "/" +
           currentRepo();
}

QString MainWindow::releasePageUrl() const
{
    return currentGiteaUrl() + "/" +
           currentOwner() + "/" +
           currentRepo() + "/releases/tag/" +
           currentTag();
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
    return currentTag();
}

bool MainWindow::validateInputs()
{
    const QString url = currentGiteaUrl();
    const QString owner = currentOwner();
    const QString repo = currentRepo();
    const QString tag = currentTag();
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
    ui->refreshOwnersButton->setEnabled(!busy && !m_ownerListReply);
    ui->ownerEdit->setEnabled(!busy);
    ui->repoEdit->setEnabled(!busy);
    ui->refreshReposButton->setEnabled(!busy && !m_repoListReply);
    ui->tagEdit->setEnabled(!busy && !m_tagListReply);
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

QStringList MainWindow::defaultOwners() const
{
    return {
        "BSP",
        "Efinix-FPGA",
        "Product",
        "Qt-Application",
        "V71N21-MCU",
        "V71Q21-MCU",
        "Xilinx-MCU"
    };
}

QString MainWindow::ownerNameFromObject(const QJsonObject &object) const
{
    const QString username = object.value("username").toString();
    if (!username.isEmpty())
        return username;

    const QString login = object.value("login").toString();
    if (!login.isEmpty())
        return login;

    return object.value("name").toString();
}

void MainWindow::requestOwnerList()
{
    const QString url = currentGiteaUrl();
    if (url.isEmpty())
        return;

    if (m_ownerListReply) {
        m_ownerListReply->abort();
        m_ownerListReply->deleteLater();
        m_ownerListReply.clear();
    }

    m_pendingOwners.clear();

    const QString token = ui->tokenEdit->text().trimmed();
    if (!token.isEmpty())
        requestOwnerListPath(OwnerListStage::FetchCurrentUser, "/api/v1/user");
    else
        requestOwnerListPath(OwnerListStage::FetchPublicOrganizations, "/api/v1/orgs");
}

void MainWindow::requestOwnerListPath(OwnerListStage stage, const QString &path)
{
    const QString url = currentGiteaUrl();
    if (url.isEmpty())
        return;

    m_ownerListStage = stage;
    QNetworkRequest req{QUrl(url + path)};
    const QString token = ui->tokenEdit->text().trimmed();
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("token " + token).toUtf8());

    ui->refreshOwnersButton->setEnabled(false);
    QNetworkReply *reply = m_ownerNet.get(req);
    m_ownerListReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleOwnerListReply(reply); });
}

void MainWindow::handleOwnerListReply(QNetworkReply *reply)
{
    if (reply != m_ownerListReply) {
        reply->deleteLater();
        return;
    }

    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const OwnerListStage stage = m_ownerListStage;
    m_ownerListStage = OwnerListStage::Idle;
    m_ownerListReply.clear();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        if (stage == OwnerListStage::FetchCurrentUser) {
            requestOwnerListPath(OwnerListStage::FetchPublicOrganizations, "/api/v1/orgs");
            return;
        }

        updateOwnerList(defaultOwners());
        logMessage("Owner list refresh failed, using defaults: " + errorString);
        ui->refreshOwnersButton->setEnabled(m_stage == Stage::Idle);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (stage == OwnerListStage::FetchCurrentUser) {
        const QString user = ownerNameFromObject(doc.object());
        if (!user.isEmpty())
            m_pendingOwners.append(user);
        requestOwnerListPath(OwnerListStage::FetchUserOrganizations, "/api/v1/user/orgs");
        return;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        const QString owner = ownerNameFromObject(value.toObject());
        if (!owner.isEmpty())
            m_pendingOwners.append(owner);
    }

    m_pendingOwners.removeDuplicates();
    m_pendingOwners.sort(Qt::CaseInsensitive);
    if (m_pendingOwners.isEmpty()) {
        updateOwnerList(defaultOwners());
        logMessage("Owner list is empty, using defaults");
    } else {
        updateOwnerList(m_pendingOwners);
        logMessage("Owner list refreshed: " + QString::number(m_pendingOwners.size()));
    }

    m_pendingOwners.clear();
    ui->refreshOwnersButton->setEnabled(m_stage == Stage::Idle);
}

void MainWindow::updateOwnerList(const QStringList &owners)
{
    const QString selected = currentOwner();
    const bool blocked = ui->ownerEdit->blockSignals(true);
    ui->ownerEdit->clear();
    ui->ownerEdit->addItems(owners);
    if (!selected.isEmpty() && owners.contains(selected))
        ui->ownerEdit->setEditText(selected);
    else {
        ui->ownerEdit->setEditText(QString());
        ui->repoEdit->setEditText(QString());
        updateTagList({}, true);
    }
    ui->ownerEdit->blockSignals(blocked);

    m_clearRepoTextOnNextRefresh = true;
    scheduleRepositoryRefresh();
}

void MainWindow::scheduleRepositoryRefresh()
{
    if (currentGiteaUrl().isEmpty() || currentOwner().isEmpty())
        return;

    if (!m_lastRepositoryRefreshOwner.isEmpty() &&
            currentOwner() != m_lastRepositoryRefreshOwner) {
        m_clearRepoTextOnNextRefresh = true;
    }

    m_repoRefreshTimer.start(350);
}

void MainWindow::scheduleTagRefresh()
{
    const QString repoKey = currentOwner() + "/" + currentRepo();
    if (currentGiteaUrl().isEmpty() || currentOwner().isEmpty() || currentRepo().isEmpty())
        return;

    if (!m_lastTagRefreshRepoKey.isEmpty() && repoKey != m_lastTagRefreshRepoKey)
        m_clearTagTextOnNextRefresh = true;

    m_tagRefreshTimer.start(350);
}

void MainWindow::requestRepositoryList(bool userEndpoint)
{
    const QString url = currentGiteaUrl();
    const QString owner = currentOwner();
    if (url.isEmpty() || owner.isEmpty())
        return;

    if (m_repoListReply) {
        m_repoListReply->abort();
        m_repoListReply->deleteLater();
        m_repoListReply.clear();
    }

    m_retryRepoListAsUser = !userEndpoint;
    if (!userEndpoint) {
        m_clearRepoTextForCurrentRefresh = m_clearRepoTextOnNextRefresh ||
                                           (!m_lastRepositoryRefreshOwner.isEmpty() &&
                                            owner != m_lastRepositoryRefreshOwner);
        m_clearRepoTextOnNextRefresh = false;
    }

    const QString encodedOwner = QString::fromUtf8(QUrl::toPercentEncoding(owner));
    const QString path = userEndpoint
                         ? "/api/v1/users/" + encodedOwner + "/repos"
                         : "/api/v1/orgs/" + encodedOwner + "/repos";

    QNetworkRequest req{QUrl(url + path)};
    const QString token = ui->tokenEdit->text().trimmed();
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("token " + token).toUtf8());

    ui->refreshReposButton->setEnabled(false);
    QNetworkReply *reply = m_repoNet.get(req);
    m_repoListReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleRepositoryListReply(reply); });
}

void MainWindow::handleRepositoryListReply(QNetworkReply *reply)
{
    if (reply != m_repoListReply) {
        reply->deleteLater();
        return;
    }

    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool canRetryAsUser = m_retryRepoListAsUser;
    m_retryRepoListAsUser = false;
    m_repoListReply.clear();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        if (status == 404 && canRetryAsUser) {
            requestRepositoryList(true);
            return;
        }

        QString msg = "Repo list refresh failed: " + errorString;
        const QString detail = extractBestErrorText(body);
        if (!detail.isEmpty())
            msg += " / " + detail;
        logMessage(msg);
        ui->refreshReposButton->setEnabled(m_stage == Stage::Idle);
        return;
    }

    QStringList repositories;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        const QString name = value.toObject().value("name").toString();
        if (!name.isEmpty())
            repositories.append(name);
    }

    repositories.removeDuplicates();
    repositories.sort(Qt::CaseInsensitive);
    updateRepositoryList(repositories, m_clearRepoTextForCurrentRefresh);
    m_clearRepoTextForCurrentRefresh = false;
    m_lastRepositoryRefreshOwner = currentOwner();
    logMessage("Repo list refreshed: " + QString::number(repositories.size()));
    ui->refreshReposButton->setEnabled(m_stage == Stage::Idle);
}

void MainWindow::updateRepositoryList(const QStringList &repositories, bool clearCurrentText)
{
    const QString selected = currentRepo();
    const bool blocked = ui->repoEdit->blockSignals(true);
    ui->repoEdit->clear();
    ui->repoEdit->addItems(repositories);
    if (clearCurrentText)
        ui->repoEdit->setEditText(QString());
    else if (!selected.isEmpty())
        ui->repoEdit->setEditText(selected);
    else if (!repositories.isEmpty())
        ui->repoEdit->setCurrentIndex(0);
    ui->repoEdit->blockSignals(blocked);

    if (clearCurrentText)
        updateTagList({}, true);
    else
        scheduleTagRefresh();
}

void MainWindow::requestTagList()
{
    const QString url = currentGiteaUrl();
    const QString owner = currentOwner();
    const QString repo = currentRepo();
    if (url.isEmpty() || owner.isEmpty() || repo.isEmpty())
        return;

    if (m_tagListReply) {
        m_tagListReply->abort();
        m_tagListReply->deleteLater();
        m_tagListReply.clear();
    }

    const QString repoKey = owner + "/" + repo;
    m_clearTagTextForCurrentRefresh = m_clearTagTextOnNextRefresh ||
                                      (!m_lastTagRefreshRepoKey.isEmpty() &&
                                       repoKey != m_lastTagRefreshRepoKey);
    m_clearTagTextOnNextRefresh = false;

    const QString encodedOwner = QString::fromUtf8(QUrl::toPercentEncoding(owner));
    const QString encodedRepo = QString::fromUtf8(QUrl::toPercentEncoding(repo));
    QNetworkRequest req{QUrl(url + "/api/v1/repos/" + encodedOwner + "/" + encodedRepo + "/tags")};
    const QString token = ui->tokenEdit->text().trimmed();
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("token " + token).toUtf8());

    ui->tagEdit->setEnabled(false);
    QNetworkReply *reply = m_tagNet.get(req);
    m_tagListReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleTagListReply(reply); });
}

void MainWindow::handleTagListReply(QNetworkReply *reply)
{
    if (reply != m_tagListReply) {
        reply->deleteLater();
        return;
    }

    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    m_tagListReply.clear();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        if (m_clearTagTextForCurrentRefresh)
            updateTagList({}, true);
        m_clearTagTextForCurrentRefresh = false;
        logMessage("Tag list refresh failed: " + errorString);
        ui->tagEdit->setEnabled(m_stage == Stage::Idle);
        return;
    }

    QStringList tags;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        const QString name = value.toObject().value("name").toString();
        if (!name.isEmpty())
            tags.append(name);
    }

    tags.removeDuplicates();
    tags.sort(Qt::CaseInsensitive);
    updateTagList(tags, m_clearTagTextForCurrentRefresh);
    m_clearTagTextForCurrentRefresh = false;
    m_lastTagRefreshRepoKey = currentOwner() + "/" + currentRepo();
    logMessage("Tag list refreshed: " + QString::number(tags.size()));
    ui->tagEdit->setEnabled(m_stage == Stage::Idle);
}

void MainWindow::updateTagList(const QStringList &tags, bool clearCurrentText)
{
    const QString selected = currentTag();
    const bool blocked = ui->tagEdit->blockSignals(true);
    ui->tagEdit->clear();
    ui->tagEdit->addItems(tags);
    if (clearCurrentText)
        ui->tagEdit->setEditText(QString());
    else if (!selected.isEmpty())
        ui->tagEdit->setEditText(selected);
    else if (!tags.isEmpty())
        ui->tagEdit->setCurrentIndex(0);
    ui->tagEdit->blockSignals(blocked);
}

void MainWindow::on_refreshReposButton_clicked()
{
    requestRepositoryList(false);
}

void MainWindow::on_refreshOwnersButton_clicked()
{
    requestOwnerList();
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
    const QString url = baseApiRepoUrl() + "/releases/tags/" + currentTag();

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
    obj["tag_name"] = currentTag();
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
