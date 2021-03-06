﻿#include "stdafx.h"
#include "BakkesModInjectorCpp.h"
#include <QtWidgets/qmessagebox.h>
#include <sstream>
#include "updatedownloader.h"
#include <tlhelp32.h>
#include "Installer.h"
#include <direct.h>
#include <QtWidgets/qshortcut.h>
#include <fstream>
#include "logger.h"
#define INJECTION_TIMEOUT_DEFAULT 2500
#include <sddl.h>
#include <stdio.h>
#include <winevt.h>


#pragma comment(lib, "wevtapi.lib")
DWORD WINAPI SubscriptionCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent);

BakkesModInjectorCpp::BakkesModInjectorCpp(QWidget *parent)
	: QMainWindow(parent)
{

	
	
	LOG_LINE(INFO, "BakkesMod injector version " << BAKKESMODINJECTOR_VERSION << ". Compiled at " __DATE__ " " __TIME__)
	ui.setupUi(this);
	LOG_LINE(INFO, "Set up UI")
	connect(&timer, SIGNAL(timeout()), this, SLOT(TimerTimeout()));
	QIcon icon;
	if(qApp->arguments().contains("icon"))
	{
		icon = QIcon(qApp->arguments().at(qApp->arguments().indexOf("icon")));
		//LOG_LINE(INFO, "Custom icon " << qApp->arguments().at(qApp->arguments().indexOf("icon")));
	}
	else {
		icon = QIcon(":/BakkesModInjectorCpp/mainicon");
	}
	this->setWindowIcon(icon);
	LOG_LINE(INFO, "Set window icon")

	auto uptime = std::chrono::milliseconds(GetTickCount64());
	auto conv = std::chrono::duration_cast<std::chrono::seconds>(uptime);
	LOG_LINE(INFO, "Time: " << conv.count())
	//Create tray icon
	trayIcon = new QSystemTrayIcon(icon, this);
	
	QMenu* menu = new QMenu();
	QAction* openAction = new QAction("Open", menu);
	QAction* closeAction = new QAction("Exit", menu);

	connect(openAction, SIGNAL(triggered()), this, SLOT(TrayOpenAction()));
	connect(closeAction, SIGNAL(triggered()), this, SLOT(TrayCloseAction()));
	//qApp->arguments().at(qApp->arguments().indexOf("icon"))
	menu->addAction(openAction);
	menu->addAction(closeAction);
	trayIcon->setContextMenu(menu);

	trayIcon->show();
	LOG_LINE(INFO, "Initialized tray icon")
	connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayClicked(QSystemTrayIcon::ActivationReason)));

	//new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_R), this, SLOT(ReleaseDLL()));
	//new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_T), this, SLOT(DebugDLL()));

}

void BakkesModInjectorCpp::initialize()
{
	timer.start(500);

	//installation.GetEpicInstallLocation();

	QApplication::setQuitOnLastWindowClosed(false);
	ui.progressBar->hide();
	LOG_LINE(INFO, "Checking if hideonboot is on")
	bool hideOnBoot = settingsManager.GetIntSetting(L"HideOnBoot");
	ui.actionMinimize_on_start->setChecked(hideOnBoot);
	if (hideOnBoot)
	{
		LOG_LINE(INFO, "Hiding to tray")
		this->showMinimized();
	}

	//Just had an update, remove the old file
	LOG_LINE(INFO, "Checking if any old versions are in current folder")

	//Duplicate code, clean this stuff up pls
	auto currentName = windowsUtils.GetCurrentExecutablePath();
	size_t loc = currentName.find_last_of('\\') + 1;
	auto newOldName = currentName.substr(0, loc) + L"bakkesmod_old.exe";

	if (WindowsUtils::FileExists(WindowsUtils::WStringToString(newOldName).c_str()))
	{
		LOG_LINE(INFO, "Found old version of bakkesmod, trying to remove")
		int removed = remove(WindowsUtils::WStringToString(newOldName).c_str());
		if (removed != 0) //Something went wrong when removing old version
		{
			DWORD lastError = GetLastError();
			LOG_LINE(INFO, "Remove error, status code: " << lastError)
		}
		else {
			LOG_LINE(INFO, "Removed file successfully")
		}
		
	}
	else
	{
		LOG_LINE(INFO, "No old version found")
	}

	int timeout = settingsManager.GetIntSetting(L"InjectionTimeout");
	if (timeout == 0)
	{
		timeout = INJECTION_TIMEOUT_DEFAULT;
		LOG_LINE(INFO, "Injection timeout not set, using default " << INJECTION_TIMEOUT_DEFAULT)
	}
	LOG_LINE(INFO, "Current injection timeout is " << timeout)
	ui.actionSet_injection_timeout->setText(QString(std::string("Set injection timeout (" + std::to_string(timeout) + ")").c_str()));


	//settingsManager.SaveSetting(L"EnableSafeMode", (int)newStatus);
}

void BakkesModInjectorCpp::changeEvent(QEvent* e)
{
	QApplication::setQuitOnLastWindowClosed(true);
	switch (e->type())
	{
	case QEvent::LanguageChange:
		//this->ui->retranslateUi(this);
		break;
	case QEvent::WindowStateChange:
	{
		if (this->windowState() & Qt::WindowMinimized)
		{
			if (settingsManager.GetIntSetting(L"HideOnMinimize")) 
			{
				QApplication::setQuitOnLastWindowClosed(false);
				QTimer::singleShot(50, this, SLOT(hide()));
			}
		}
		break;
	}
	default:
		break;
	}

	QMainWindow::changeEvent(e);
}

void BakkesModInjectorCpp::OpenWebsite(std::string url)
{
	ShellExecute(0, 0, WindowsUtils::StringToWString(url).c_str(), 0, 0, SW_SHOW);
}

void BakkesModInjectorCpp::SetState(BakkesModStatus newState)
{
	LOG_LINE(INFO, "Switching from " << GetStateName(bakkesModState) << " (" << bakkesModState << ")" << " to " << GetStateName(newState) << " (" << newState << ")")
	bakkesModState = newState;
}

std::string BakkesModInjectorCpp::GetStatusString()
{
	std::string status = "";
	switch (bakkesModState)
	{
	case BOOTING:
		status = "Starting...";
		break;
	case BAKKESMOD_IDLE:
		status = bakkesModIdleString;
		break;
	case WAITING_FOR_RL:
		break;
	case OUT_OF_DATE:
		status = "Out of date";
		break;
	case OUT_OF_DATE_SAFEMODE_ENABLED:
		status = bakkesModOutOfDateSafeModeEnabledString;
		break;
	case CHECKING_FOR_UPDATES:
		status = "Checking for updates";
		break;
	case START_UPDATING:
		status = "Starting update process";
		break;
	case UPDATING_BAKKESMOD:
		status = "Updating BakkesMod";
		break;
	case UPDATING_INJECTOR:
		status = "Updating Injector";
		break;
	case INJECT_DLL:
		status = "Injecting DLL";
		break;
	case INJECTED:
		status = bakkesModInjectedString;
		break;
	case INJECTION_FAILED:
		status = "Injection failed, please download vc_redist.x86.exe and restart your PC";
		break;
	case BAKKESMOD_INSTALLING:
		status = "Extracting archive";
		break;
	case REINSTALL:
		status = "Reinstalling BakkesMod";
		break;
	case INSTALLATION_CORRUPT:
		status = "Installation corrupted (AV issue?), please reinstall (File -> reinstall)";
		break;
	case WAIT_FOR_RL_CLOSE:
		status = "Postponing update until RL is closed...";
		break;
	}
	return status;
}

void BakkesModInjectorCpp::OnCheckInjection() 
{
	DWORD injectionResult = dllInjector.IsBakkesModDllInjected(RL_PROCESS_NAME);
	QMessageBox msgBox;
	
	if (injectionResult == NOT_RUNNING)
	{
		msgBox.setText("Rocket League is not running");
	}
	else if (injectionResult == NOPE)
	{
		msgBox.setText("BakkesMod DLL not found in Rocket League process");
	}
	else if (injectionResult == OK)
	{
		msgBox.setText("Injection successful");
	}
	else
	{
		msgBox.setText("??");
	}
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setDefaultButton(QMessageBox::Ok);
	int ret = msgBox.exec();
}

void BakkesModInjectorCpp::TimerTimeout()
{

	static UpdateDownloader* updateDownloader = NULL;
	static std::chrono::steady_clock::time_point lastSync = std::chrono::steady_clock::now();
	static std::chrono::steady_clock::time_point lastVersionCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10000);
	static std::chrono::steady_clock::time_point lastInstallationCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10000);
	//LOG_LINE(INFO, GetStateName(bakkesModState))
	switch (bakkesModState)
	{
	case BOOTING:
	{

		

		LOG_LINE(INFO, "Booting")

		{
			installation.CreateAppDataFolderIfDoesntExist();
			auto oldInstall = installation.GetOldBakkesModFolder();
			auto newInstall = installation.GetBakkesModFolder();
			LOG_LINE_W(INFO, L"OLD " << oldInstall.wstring());

			LOG_LINE_W(INFO, L"NEW " << newInstall.wstring());
			if (oldInstall.wstring().size() > 0)
			{
				LOG_LINE_W(INFO, L"OLD INSTALL DETECTED " << oldInstall.wstring());
				if (!std::filesystem::exists(newInstall))
				{
					if (std::filesystem::exists(oldInstall))
					{
						try
						{
							LOG_LINE_W(INFO, "Creating directory " << newInstall.wstring());
							std::filesystem::create_directories(newInstall);
							LOG_LINE_W(INFO, "Copying directory " << oldInstall.wstring() << "->" << newInstall.wstring());
							std::filesystem::copy(oldInstall, newInstall, std::filesystem::copy_options::recursive);
							LOG_LINE_W(INFO, "Removing old directory " << oldInstall.wstring());
							std::filesystem::remove_all(oldInstall);
						}
						catch (std::exception& e)
						{
							LOG_LINE(INFO, "Something went wrong on migration: " << e.what())
						}
					}
				}
			}
		}

		if (!installation.IsInstalled())
		{
			LOG_LINE(INFO, "Not installed, setting default values")
				settingsManager.SaveSetting(L"EnableSafeMode", 1);
			settingsManager.SaveSetting(L"HideOnMinimize", 1);
			settingsManager.SaveSetting(L"HideOnBoot", 0);
			settingsManager.SaveSetting(L"DisableWarnings", 0);
			settingsManager.SaveSetting(L"BakkesMod", L"-", RegisterySettingsManager::REGISTRY_DIR_RUN);
			LOG_LINE(INFO, "Default settings set")
				//settingsManager.SaveSetting(L"InjectionTimeout", 70);
		}
		
		{
			auto currentPid = ::getpid();
			auto ids = dllInjector.GetProcessIDS(L"bakkesmod.exe");
			{
				auto ids2 = dllInjector.GetProcessIDS(L"bakkesmodinjector.exe");
				ids.insert(ids.end(), ids2.begin(), ids2.end());
			}
			for (auto id : ids)
			{
				if (id == currentPid)
					continue;
				LOG_LINE(INFO, "Found existing BM process " << id);
				LOG_LINE(INFO, "Found existing BM process, PID: " << id << ". Currently on try 1");
				HANDLE rlHandle;
				rlHandle = OpenProcess(PROCESS_ALL_ACCESS, false, id);
				LOG_LINE(INFO, "Sending terminate");
				TerminateProcess(rlHandle, 1);
				LOG_LINE(INFO, "Waiting for Single Object (timeout=1000ms)");
				WaitForSingleObject(rlHandle, 1000);
				LOG_LINE(INFO, "Done waiting");
				CloseHandle(rlHandle);
				LOG_LINE(INFO, "Closed handle");
				
			}
		}

		ui.actionEnable_safe_mode->setChecked(settingsManager.GetIntSetting(L"EnableSafeMode"));
		safeModeEnabled = ui.actionEnable_safe_mode->isChecked();
		ui.actionHide_when_minimized->setChecked(settingsManager.GetIntSetting(L"HideOnMinimize"));
		ui.actionDisable_warnings->setChecked(settingsManager.GetIntSetting(L"DisableWarnings"));

		bool betaEnabled = settingsManager.GetIntSetting(L"EnableBeta");
		ui.actionOpt_in_to_beta_releases->setChecked(betaEnabled);
		updater.SetEnableBeta(betaEnabled);
		
		bool runOnStartup = !settingsManager.GetStringSetting(L"BakkesMod", RegisterySettingsManager::REGISTRY_DIR_RUN).empty();
		ui.actionRun_on_startup->setChecked(runOnStartup);
		OnRunOnStartup();
		int version = installation.GetVersion();
		updater.CheckForUpdates(version);
		SetState(CHECKING_FOR_UPDATES);
		LOG_LINE(INFO, "Checking for updates, current version = " << version);
		timer.setInterval(500);
	}
		break;
	case CHECKING_FOR_UPDATES:
	{
		lastInstallationCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10000);
		if (updater.latestUpdateInfo.requestFinished)
		{
			LOG_LINE(INFO, "Request finished")
			if (updater.latestUpdateInfo.networkRequestStatus == FINISHED_ERROR)
			{
				LOG_LINE(WARNING, "Error connecting to update server")
				SetState(BAKKESMOD_IDLE);

				bakkesModIdleString = "Error connecting to update server,\nwill retry in a few minutes";
				bakkesModOutOfDateSafeModeEnabledString = "Error connecting to update server,\nwill retry in a few minutes";
				/*QMessageBox msgBox2;
				std::stringstream ss;
				msgBox2.setText("Could not connect to update server, running in offline mode");
				msgBox2.setStandardButtons(QMessageBox::Ok);
				msgBox2.setDefaultButton(QMessageBox::Ok);
				int ret = msgBox2.exec();*/
				return;
			}
			else
			{
				bakkesModOutOfDateSafeModeEnabledString = "Mod is out of date, waiting for an update";
				LOG_LINE(INFO, "Successfully received response from BakkesMod update server")
				if (std::stoi(updater.latestUpdateInfo.injectorVersion) > BAKKESMODINJECTOR_VERSION)
				{
					LOG_LINE(INFO, "Injector out of date, current version = " << BAKKESMODINJECTOR_VERSION << " whilst " << updater.latestUpdateInfo.injectorVersion << " is available")
					QMessageBox msgBox;

					std::stringstream ss;
					ss << "An new version of the injector is available " << std::endl;
					ss << "Would you like to download?";
					msgBox.setText(ss.str().c_str());
					msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
					msgBox.setDefaultButton(QMessageBox::Yes);
					int ret = msgBox.exec();
					if (ret == QMessageBox::Yes)
					{
						LOG_LINE(INFO, "User accepted injector update")
						SetState(UPDATING_INJECTOR);
						if (windowsUtils.IsStandalone())
						{
							updateDownloader = new UpdateDownloader(updater.latestUpdateInfo.injectorUrl, "newbakkesmod.exe");
						}
						else
						{
							updateDownloader = new UpdateDownloader(updater.latestUpdateInfo.injectorSetupUrl, "newbakkesmodsetup.exe");
						}
						
						updateDownloader->StartDownload();
						ui.progressBar->setMaximum(100);
						return;
					}
					LOG_LINE(INFO, "User denied injector update")
				}
				ui.actionOpt_in_to_beta_releases->setEnabled(updater.latestUpdateInfo.allowBetaAccess);
				//out of date counter = 0 -> new run. >0 -> Checking for updates
				if (updater.latestUpdateInfo.requiresUpdate)
				{
					if (outOfDateCounter == 0 || dllInjector.GetProcessID64(RL_PROCESS_NAME) == 0)
					{
						LOG_LINE(INFO, "BakkesMod update available, version " << updater.latestUpdateInfo.newestTrainerVersion)
							QMessageBox msgBox;

						std::stringstream ss;
						ss << "An update is available: " << std::endl;
						ss << updater.latestUpdateInfo.updateMessage << std::endl;
						ss << "Would you like to update?";
						msgBox.setText(ss.str().c_str());
						msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
						msgBox.setDefaultButton(QMessageBox::Yes);
						int ret = msgBox.exec();
						if (ret == QMessageBox::Yes)
						{
							LOG_LINE(INFO, "User accepted update, downloading now...")
								SetState(UPDATING_BAKKESMOD);
							LOG_LINE(INFO, "Constructing update downloader...")
								updateDownloader = new UpdateDownloader(updater.latestUpdateInfo.downloadUrl, "bmupdate.zip");
							LOG_LINE(INFO, "Telling downloader to download...")
								updateDownloader->StartDownload();
							LOG_LINE(INFO, "Told update downloader to download...")
								ui.progressBar->setMaximum(100);
							return;
						}
						else
						{
							LOG_LINE(INFO, "User cancelled update")
								SetState(CHECK_D3D9);
							QMessageBox msgBox2;
							std::stringstream ss;
							msgBox2.setText("Update cancelled, mod might not work now though");
							msgBox2.setStandardButtons(QMessageBox::Ok);
							msgBox2.setDefaultButton(QMessageBox::Ok);
							int ret = msgBox2.exec();
							LOG_LINE(INFO, "Shown message about mod not working")
						}
					}
					else
					{
						LOG_LINE(INFO, "Update available but user is still playing RL after being out of date, not prompting")
						timer.setInterval(8000);
					}
				}
				else
				{
					LOG_LINE(INFO, "No update available, switching to IDLE status")
					SetState(CHECK_D3D9);
				}
			}
			LOG_LINE(INFO, "Safe mode status: " << safeModeEnabled);
			
			if (safeModeEnabled && !installation.IsSafeToInject(updater.latestUpdateInfo)) //Check if out of date
			{
				SetState(OUT_OF_DATE_SAFEMODE_ENABLED);
				LOG_LINE(INFO, "BakkesMod does not support newest update yet, still stuck on old version " << updater.latestUpdateInfo.buildID);
			}

		}
		else {

		}
	}
		break;
	case OUT_OF_DATE_SAFEMODE_ENABLED:
	{
		outOfDateCounter++;
		timer.setInterval(1000);
		if (updater.checkForNetworkChange())
		{
			LOG_LINE(INFO, "Detected change in internet accessibility")
			updater.latestUpdateInfo = UpdateStatus();
			SetState(BOOTING);
			outOfDateCounter = 1;
		}
		else if (outOfDateCounter > updater.latestUpdateInfo.backoff_seconds_outofdate)
		{
			LOG_LINE(INFO, "Checking if BakkesMod has updated yet")
			updater.latestUpdateInfo = UpdateStatus();
			SetState(BOOTING);
			outOfDateCounter = 1;
		}
		if (!safeModeEnabled)
		{
			LOG_LINE(INFO, "User disabled safe mode whilst mod is out of date, going to IDLE state")
			SetState(BAKKESMOD_IDLE);
		}

	}
		break;
	case UPDATING_INJECTOR:
	{
		if (!updateDownloader)
			return;
		if (updateDownloader->completed)
		{
			LOG_LINE(INFO, "Injector update download complete")
			ui.progressBar->hide();
			auto currentName = windowsUtils.GetCurrentExecutablePath();
			LOG_LINE(INFO, "Current executable path is " << WindowsUtils::WStringToString(currentName).c_str())
			size_t loc = currentName.find_last_of('\\') + 1;
			auto newOldName = currentName.substr(0, loc) + L"bakkesmod_old.exe";
			LOG_LINE(INFO, "Renaming current injector to " << WindowsUtils::WStringToString(newOldName).c_str())
				LOG_LINE(INFO, "Is standalone " << windowsUtils.IsStandalone());
				if (windowsUtils.IsStandalone())
				{
					if (MoveFile(currentName.c_str(), newOldName.c_str()) != 0)
					{
						LOG_LINE(INFO, "Successfully renamed current executable to bakkesmod_old.exe");
						if (MoveFile(updateDownloader->packageUrl.c_str(), currentName.c_str()) != 0)
						{
							LOG_LINE(INFO, "Moved new injector from " << WindowsUtils::WStringToString(updateDownloader->packageUrl) << " to " << WindowsUtils::WStringToString(currentName));

							STARTUPINFO si;
							PROCESS_INFORMATION pi;
							ZeroMemory(&si, sizeof(si)); //Use default startup info
							ZeroMemory(&pi, sizeof(pi));
							LPWSTR commandLine = L""; //No command line arguments
							CreateProcess(currentName.c_str(),
								commandLine,
								NULL,
								NULL,
								FALSE,
								CREATE_BREAKAWAY_FROM_JOB, //Actually launch new process since we close this one on the next line
								NULL,
								NULL,
								&si,
								&pi
							);

							LOG_LINE(INFO, "Result " << GetLastError());//For now, I guess we do nothing if launching new process didn't work
							//system(WindowsUtils::WStringToString(currentName).c_str());
							QCoreApplication::quit();
						}
						else
						{
							LOG_LINE(INFO, "Failed to rename old file to current: " << GetLastError())
						}
					}
					else
					{
						LOG_LINE(INFO, "Unable to rename current executable to bakkesmod_old.exe, displaying error to user");
						QMessageBox msgBox;
						msgBox.setText("Could not update BakkesMod injector, no permissions to rename file, sorry! Try downloading it manually from the website");
						msgBox.setStandardButtons(QMessageBox::Ok);
						msgBox.setDefaultButton(QMessageBox::Ok);
						int ret = msgBox.exec();
						SetState(BAKKESMOD_IDLE);
					}
				}
				else
				{
					STARTUPINFO si;
					PROCESS_INFORMATION pi;
					ZeroMemory(&si, sizeof(si)); //Use default startup info
					ZeroMemory(&pi, sizeof(pi));
					LPWSTR commandLine = L" /installfromdll=true /VERYSILENT"; //No command line arguments
					auto created = CreateProcess(updateDownloader->packageUrl.c_str(),
						commandLine,
						NULL,
						NULL,
						FALSE,
						NULL, //Actually launch new process since we close this one on the next line
						NULL,
						NULL,
						&si,
						&pi
					);

					LOG_LINE(INFO, "Result " << GetLastError());//For now, I guess we do nothing if launching new process didn't work
					auto waited = WaitForSingleObject(pi.hProcess, 15000);
					LOG_LINE(INFO, "Done waiting " << waited << ", " << GetLastError());//For now, I guess we do nothing if launching new process didn't work
					SetState(BAKKESMOD_IDLE);
				}
			
		}
		else {
			ui.progressBar->show();
			ui.progressBar->setValue(UpdateDownloader::percentComplete);

		}
	}
	break;
	case UPDATING_BAKKESMOD:
		if (!updateDownloader)
			return;

		if (updateDownloader->completed)
		{
			LOG_LINE(INFO, "Downloaded BakkesMod update file")
			ui.progressBar->hide();
			SetState(BAKKESMOD_INSTALLING);
		}
		else {
			timer.setInterval(100);
			ui.progressBar->show();
			LOG_LINE(INFO, "[MainLoop] Update progress: " << UpdateDownloader::percentComplete << "%")
			ui.progressBar->setValue(UpdateDownloader::percentComplete);

		}
	break;
	case WAIT_FOR_RL_CLOSE:
	{
		timer.setInterval(2500);
		DWORD processId = dllInjector.GetProcessID64(RL_PROCESS_NAME);
		if (processId == 0)
		{
			SetState(BAKKESMOD_INSTALLING);
		}
	}
		break;
	case BAKKESMOD_INSTALLING:
	{
		if (installation.IsInstalled() && !PopupRLRunningTillClosed())
		{
			QMessageBox msgBox;
			std::stringstream ss;
			ss << "Cannot install/update BakkesMod while Rocket League is running! Mod will be updated when RL is closed." << std::endl;
			msgBox.setText(ss.str().c_str());
			msgBox.setStandardButtons(QMessageBox::Ok);
			msgBox.setDefaultButton(QMessageBox::Ok);
			int ret = msgBox.exec();
			SetState(WAIT_FOR_RL_CLOSE);
			return;
		}
		Installer i(updateDownloader->packageUrl, installation.GetBakkesModFolder());
		i.Install();

		{
			std::ofstream out(installation.GetBakkesModFolder() / "injectorversion.txt");
			out << BAKKESMODINJECTOR_VERSION;
			out.close();
		}
		
		{
			std::ofstream out(installation.GetBakkesModFolder() / "updaterinfo.txt");
			out << updater.latestUpdateInfo.jsonData;
			out.close();
		}
		LOG_LINE(INFO, "Writing " << BAKKESMODINJECTOR_VERSION << " to injectorversion.txt")
		SetState(CHECK_D3D9);
	}
		break;
	case BAKKESMOD_IDLE:
	{
		timer.setInterval(3000);
		static int intervalLoops = 0;

		auto now = std::chrono::steady_clock::now();
		auto updateDiff = std::chrono::duration_cast<std::chrono::seconds>(now - lastSync).count();
		auto versionCheckDiff = std::chrono::duration_cast<std::chrono::seconds>(now - lastVersionCheck).count();
		auto lastInstallationDiff = std::chrono::duration_cast<std::chrono::seconds>(now - lastInstallationCheck).count();
		if (updateDiff > updater.latestUpdateInfo.backoff_seconds)
		{
			OnCheckForUpdates();
			lastSync = now;
		}

		if (lastInstallationDiff > 30)
		{
			lastInstallationCheck = now;
			auto hasSteamVersion = installation.IsSteamVersionInstalled();
			auto hasEgsVersion = installation.IsEpicVersionInstalled();
			if (!hasSteamVersion && !hasEgsVersion)
			{
				bakkesModIdleString = "No RL installation detected";
			}
			else
			{
				std::stringstream ss;
				bool canInject = false;
				if (hasSteamVersion)
				{
					if (installation.IsSteamVersionReady(updater.latestUpdateInfo))
					{
						ss << "Rocket League (Steam): up to date.\n";
						canInject = true;
					}
					else
					{
						ss << "Rocket League (Steam): BM out of date.\n";
					}
				}
				if (hasEgsVersion)
				{
					if (installation.IsEpicVersionReady(updater.latestUpdateInfo))
					{
						ss << "Rocket League (Epic Games): up to date.\n";
						canInject = true;
					}
					else
					{
						ss << "Rocket League (Epic Games): BM out of date.\n";
					}
				}
				if (canInject)
				{
					ss << "Uninjected, waiting for user to start Rocket League";
				}
				bakkesModIdleString = ss.str();
			}
		}
		
		if (versionCheckDiff > 200)
		{
			lastVersionCheck = now;
			if (safeModeEnabled && !installation.IsSafeToInject(updater.latestUpdateInfo)) //Check if out of date every X sec
			{
				SetState(OUT_OF_DATE_SAFEMODE_ENABLED);
			}
		}
		else if (dllInjector.GetProcessID64(RL_PROCESS_NAME))
		{
			if (safeModeEnabled && !installation.IsSafeToInject(updater.latestUpdateInfo)) //Check if safe to inject right before injecting
			{
				SetState(OUT_OF_DATE_SAFEMODE_ENABLED);
			}
			else
			{
				LOG_LINE(INFO, "Rocket league process ID is " << dllInjector.GetProcessID64(RL_PROCESS_NAME))
				SetState(INJECT_DLL);
				int timeout = settingsManager.GetIntSetting(L"InjectionTimeout");
				if (timeout == 0)
					timeout = INJECTION_TIMEOUT_DEFAULT;
				timer.setInterval(timeout);

				//This stuff is now ignored by the DLL, keeping it for future stuff though.
				if (intervalLoops < 10)
				{
					dllInjector.SetInjectionParameters({ BAKKESMODINJECTOR_VERSION, 0 });
				}
				else
				{
					dllInjector.SetInjectionParameters({ BAKKESMODINJECTOR_VERSION, 1 });
				}
			}

		}
	}
		break;
	case INJECT_DLL:
	{
		auto bmPath = installation.GetBakkesModFolder();
		auto path = bmPath / "dll" / "bakkesmod.dll";
		LOG_LINE_W(INFO, L"Trying to inject " << path.wstring())
		if (!WindowsUtils::FileExists(path))
		{
			QMessageBox msgBox;
			msgBox.setText("Could not find BakkesMod DLL, this is most likely due to your antivirus removing it. Please whitelist the BakkesMod folder and injector, then try reinstalling (File -> reinstall)");
			msgBox.setStandardButtons(QMessageBox::Ok);
			msgBox.setDefaultButton(QMessageBox::Ok);
			int ret = msgBox.exec();
			SetState(INSTALLATION_CORRUPT);
			return;
		}
		{
			std::ofstream out(installation.GetBakkesModFolder() / "updaterinfo.txt");
			out << updater.latestUpdateInfo.jsonData;
			out.close();
		}

		DWORD alreadyInjected = dllInjector.IsBakkesModDllInjected(RL_PROCESS_NAME);
		LOG_LINE(INFO, "Already injected check: " <<(int)alreadyInjected)
		if (alreadyInjected == OK)
		{
			SetState(INJECTED);
			break;
		}
		
		//GetPlatform
		GamePlatform platform = dllInjector.GetPlatform(dllInjector.GetProcessID64(RL_PROCESS_NAME));

		bool isSafeToInject = false;
		switch (platform)
		{
			case GamePlatform::STEAM:
				bakkesModInjectedString = "Injected, press F2 ingame for options menu (Steam version)";
				isSafeToInject = installation.IsSteamVersionReady(updater.latestUpdateInfo);
				break;
			case GamePlatform::EPIC:
				bakkesModInjectedString = "Injected, press F2 ingame for options menu (Epic Games version)";
				isSafeToInject = installation.IsEpicVersionReady(updater.latestUpdateInfo);
				break;
			case GamePlatform::UNKNOWN:
				bakkesModInjectedString = "Injected, press F2 ingame for options menu (Unknown)";
				isSafeToInject = installation.IsSafeToInject(updater.latestUpdateInfo);
				break;
		}
		if (!isSafeToInject)
		{
			LOG_LINE(INFO, "Probably not safe to inject");
			{
				QMessageBox msgBox;
				std::stringstream ss;
				ss << "It seems like the mod is trying to inject in an unsupported version of Rocket League." << std::endl;
				ss << "Would you like to try to inject anyway? This may cause instability issues and crashes";
				msgBox.setText(ss.str().c_str());
				msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
				msgBox.setDefaultButton(QMessageBox::Yes);
				int ret = msgBox.exec();
				if (ret == QMessageBox::Yes)
				{
					LOG_LINE(INFO, "User chooses to inject anyway")
				}
				else
				{
					LOG_LINE(INFO, "User denied injection")
					SetState(INJECTED);
					bakkesModInjectedString = "User denied injection. Restart the game to try again";
					return;
				}
			}
			
		}
		dllInjector.InjectDLL(RL_PROCESS_NAME, path);

		DWORD injectionResult = dllInjector.IsBakkesModDllInjected(RL_PROCESS_NAME);
		if (injectionResult == NOPE)
		{
			SetState(INJECTION_FAILED);
			QMessageBox msgBox;
			std::stringstream ss;
			ss << "It looks like injection was unsuccessful, this probably means you're missing a dependency (vc_redist.x64.exe). Please download it from http://bakkesmod.wikia.com/wiki/Troubleshooting" << std::endl;
			ss << "Would you like to open it now?";
			msgBox.setText(ss.str().c_str());
			msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
			msgBox.setDefaultButton(QMessageBox::Yes);
			int ret = msgBox.exec();
			if (ret == QMessageBox::Yes)
			{
				OpenWebsite("http://bakkesmod.wikia.com/wiki/Troubleshooting");
			}
		}
		else
		{
			SetState(INJECTED);
		}
	}
		break;
	case INJECTED:
		timer.setInterval(1000);
		if (!dllInjector.GetProcessID64(RL_PROCESS_NAME))
		{
			SetState(BAKKESMOD_IDLE);
		}
		break;
	case CHECK_D3D9:
	{
		if (settingsManager.GetIntSetting(L"DisableWarnings") == 0 && WindowsUtils::FileExists(installation.GetBakkesModFolder() / "../d3d9.dll"))
		{
			QMessageBox msgBox;
			LOG_LINE(INFO, "found a d3d9.dll")
				std::stringstream ss;
			ss << "WARNING: d3d9.dll detected. This file is used in reshade and might prevent BakkesMod GUI/new console from working. Would you like BakkesMod to remove this file?" << std::endl;
			msgBox.setText(ss.str().c_str());
			msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
			msgBox.setDefaultButton(QMessageBox::Yes);
			int ret = msgBox.exec();
			if (ret == QMessageBox::Yes)
			{
				if (std::filesystem::remove((installation.GetBakkesModFolder() / "../d3d9.dll").c_str()) != 0) 
				{
					QMessageBox msgBox2;
					LOG_LINE(INFO, "Could not remove d3d9.dll")
						std::stringstream ss;
					ss << "Unable to remove file, file already in use? BakkesMod GUI will most likely not work" << std::endl;
					msgBox2.setText(ss.str().c_str());
					msgBox2.setStandardButtons(QMessageBox::Ok);
					msgBox2.setDefaultButton(QMessageBox::Ok);
					int ret = msgBox2.exec();
				}
				else
				{
					QMessageBox msgBox2;
					LOG_LINE(INFO, "d3d9.dll removed")
						std::stringstream ss;
					ss << "Removed d3d9.dll!" << std::endl;
					msgBox2.setText(ss.str().c_str());
					msgBox2.setStandardButtons(QMessageBox::Ok);
					msgBox2.setDefaultButton(QMessageBox::Ok);
					int ret = msgBox2.exec();
				}
			}
		}
		/*if (!installation.ManifestFileExists() && ui.actionEnable_safe_mode->isChecked())
		{
			QMessageBox msgBox;
			LOG_LINE(INFO, "Could not find manifest file")
				std::stringstream ss;
			ss << "Unable to find manifest file, disabling safe mode" << std::endl << "Warning: might result in instability after a RL update";
			msgBox.setText(ss.str().c_str());
			msgBox.setStandardButtons(QMessageBox::Ok);
			msgBox.setDefaultButton(QMessageBox::Ok);
			int ret = msgBox.exec();
			ui.actionEnable_safe_mode->setChecked(false);
			OnCheckSafeMode();
		}*/
		SetState(BAKKESMOD_IDLE);
	}
		break;
	}
	std::string sstr = GetStatusString().c_str();
	if(ui.label)
		ui.label->setText(QString(sstr.c_str()));
}

void BakkesModInjectorCpp::OnCheckSafeMode()
{
	bool newStatus = ui.actionEnable_safe_mode->isChecked();
	//ui.actionEnable_safe_mode->setChecked(newStatus);
	settingsManager.SaveSetting(L"EnableSafeMode", (int)newStatus);
	safeModeEnabled = newStatus;
}

void BakkesModInjectorCpp::OnMinimizeOnStart()
{
	bool newStatus = ui.actionMinimize_on_start->isChecked();
	//ui.actionMinimize_on_start->setChecked(newStatus);
	settingsManager.SaveSetting(L"HideOnBoot", (int)newStatus);
}

void BakkesModInjectorCpp::OnHideOnMinimize()
{
	bool newStatus = ui.actionHide_when_minimized->isChecked();
	//ui.actionHide_when_minimized->setChecked(newStatus);
	settingsManager.SaveSetting(L"HideOnMinimize", (int)newStatus);
}

void BakkesModInjectorCpp::OnRunOnStartup()
{
	bool newStatus = ui.actionRun_on_startup->isChecked();
	
	if (newStatus)
	{
		std::wostringstream w;
		w << "\"" << windowsUtils.GetCurrentExecutablePath() << "\"";
		settingsManager.SaveSetting(L"BakkesMod", w.str(), RegisterySettingsManager::REGISTRY_DIR_RUN);
	}
	else
	{
		settingsManager.DeleteSetting(L"BakkesMod", RegisterySettingsManager::REGISTRY_DIR_RUN);
	}
}

void BakkesModInjectorCpp::OnDisableWarnings()
{
	bool newStatus = ui.actionDisable_warnings->isChecked();
	settingsManager.SaveSetting(L"DisableWarnings", (int)newStatus);
}

void BakkesModInjectorCpp::OnExitClick()
{
	QApplication::exit();
}

void BakkesModInjectorCpp::OnReinstallClick()
{
	QMessageBox msgBox;
	LOG_LINE(INFO, "User requested reinstall")
	std::stringstream ss;
	ss << "This will remove all existing BakkesMod files, are you sure you want to continue?" << std::endl;
	msgBox.setText(ss.str().c_str());
	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	msgBox.setDefaultButton(QMessageBox::Yes);
	int ret = msgBox.exec();
	if (ret == QMessageBox::Yes)
	{
		if (std::filesystem::exists(installation.GetBakkesModFolder()))
		{
			if (WindowsUtils::DeleteDirectory(installation.GetBakkesModFolder()) == 0)
			{
				LOG_LINE(INFO, "Could not remove BakkesMod folder")
					QMessageBox msgBox2;
				msgBox2.setText("Could not remove BakkesMod folder, no rights or a file is already in use.\nAborting reinstallation.");
				msgBox2.setStandardButtons(QMessageBox::Ok);
				msgBox2.setDefaultButton(QMessageBox::Ok);
				int ret = msgBox2.exec();
				return;
			}
		}
		{
			LOG_LINE(INFO, "Reinstalling BakkesMod")
			updater.latestUpdateInfo = UpdateStatus();
			settingsManager.DeleteSetting(L"BakkesModPath", RegisterySettingsManager::REGISTRY_DIR_APPPATH);
			settingsManager.DeleteSetting(L"EnableSafeMode");
			settingsManager.DeleteSetting(L"EnableBeta");
			settingsManager.DeleteSetting(L"HideOnMinimize");
			settingsManager.DeleteSetting(L"HideOnBoot");
			settingsManager.DeleteSetting(L"DisableWarnings");
			settingsManager.DeleteSetting(L"BakkesMod", RegisterySettingsManager::REGISTRY_DIR_RUN);
			installation.resetBMFolder();
			SetState(BOOTING);
		}
	}
	else {
		LOG_LINE(INFO, "User cancelled reinstall")
	}
}

void BakkesModInjectorCpp::OnPythonInstallClick()
{
	QMessageBox msgBox;

	std::stringstream ss;
	ss << "Not supported yet." << std::endl;
	msgBox.setText(ss.str().c_str());
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setDefaultButton(QMessageBox::Ok);
	int ret = msgBox.exec();
}

void BakkesModInjectorCpp::OnPatreonClick()
{
	OpenWebsite("https://patreon.com/bakkesmod");
}

void BakkesModInjectorCpp::OnBetaChannelClick()
{
	bool newStatus = ui.actionOpt_in_to_beta_releases->isChecked();
	if (newStatus)
	{
		QMessageBox msgBox2;
		msgBox2.setText("The use of beta releases could make your game unstable, if you run into issues, please report them.");
		msgBox2.setStandardButtons(QMessageBox::Ok);
		msgBox2.setDefaultButton(QMessageBox::Ok);
		int ret = msgBox2.exec();
	}
	settingsManager.SaveSetting(L"EnableBeta", (int)newStatus);
	safeModeEnabled = newStatus;
	updater.SetEnableBeta(newStatus);
	std::filesystem::path versionFilePath = installation.GetBakkesModFolder() / "version.txt";
	if (windowsUtils.FileExists(versionFilePath))
	{
		std::ofstream versionFile;
		versionFile.open(versionFilePath);
		int version = 1;
		versionFile << version;
		versionFile.close();
		LOG_LINE_W(INFO, "Writing version " << version << " to " << versionFilePath.wstring());
		OnCheckForUpdates();
	}

}

void BakkesModInjectorCpp::OpenTwitter()
{
	OpenWebsite("https://twitter.com/bakkesmod");
}

void BakkesModInjectorCpp::OpenDiscord()
{
	OpenWebsite("https://discord.gg/HsM6kAR");
}

void BakkesModInjectorCpp::OpenWebsite()
{
	OpenWebsite("http://bakkesmod.com");
}

void BakkesModInjectorCpp::OpenTroubleshootPage()
{
	
	OpenWebsite("http://bakkesmod.wikia.com/wiki/Troubleshooting");
}


void BakkesModInjectorCpp::trayClicked(QSystemTrayIcon::ActivationReason e)
{
	if (e == QSystemTrayIcon::Trigger) {
		if (this->isVisible()) 
			this->hide();
		else {
			setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
			raise();  // for MacOS
			activateWindow(); // for Windows
			this->show();
		}
	}
}

void BakkesModInjectorCpp::TrayOpenAction()
{
	setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	raise();
	activateWindow();
	this->show();
}

void BakkesModInjectorCpp::TrayCloseAction()
{
	QApplication::exit();
}
#include <QtWidgets/qfiledialog.h>
void BakkesModInjectorCpp::OnSelectBakkesModFolder()
{
	QString path = QFileDialog::getOpenFileName(this,
		QString("Select the Rocket League executable"), QString(QDir::currentPath()), QString("RocketLeague.exe (RocketLeague.exe)"));
	std::string rlPath = path.toStdString().substr(0, path.size() - std::string("RocketLeague.exe").size());
	auto converted = windowsUtils.StringToWString(rlPath + "bakkesmod\\");
	settingsManager.SaveSetting(L"BakkesModPath", converted, RegisterySettingsManager::REGISTRY_DIR_APPPATH);
}

void BakkesModInjectorCpp::OnCheckForUpdates()
{
	SetState(BOOTING);
}

#include <QtWidgets/qinputdialog.h>
void BakkesModInjectorCpp::OnSetInjectionTimeout()
{
	bool ok;
	int timeout = settingsManager.GetIntSetting(L"InjectionTimeout");
	if (timeout == 0)
	{
		timeout = INJECTION_TIMEOUT_DEFAULT;
	}
	QString text = QInputDialog::getText(this, "Set injection timeout",
		QString(std::string("Set the injection timeout (" + std::to_string(INJECTION_TIMEOUT_DEFAULT) + " = default). Set it to a higher value if you're experiencing crashes during launch.").c_str()), 
		QLineEdit::Normal, QString(std::to_string(timeout).c_str()), &ok);
	if (ok && !text.isEmpty()) {
		std::string test = text.toStdString();
		int intval = std::stoi(test);

		ui.actionSet_injection_timeout->setText(QString(std::string("Set injection timeout (" + std::to_string(intval) + ")").c_str()));
		settingsManager.SaveSetting(L"InjectionTimeout", intval);
	}
}

void BakkesModInjectorCpp::OnShowVersionInfo()
{
	std::stringstream ss;
	ss << "Injector version: " << BAKKESMODINJECTOR_VERSION << "\n";
	ss << "BakkesMod version: " << installation.GetVersion() << "\n";
	ss << "Installed Steam version: " << installation.GetSteamVersion() << "\n";
	ss << "Installed Epic version: " << installation.GetHighestEpicVersion() << "\n";

	ss << "\nBakkesMod supported versions:\n";
	ss << "Steam: ";
	for (auto bid : updater.latestUpdateInfo.buildIds)
	{
		ss << bid << ", ";
	}
	ss << "\n";
	ss << "Epic: ";
	for (auto bid : updater.latestUpdateInfo.egsBuildIds)
	{
		ss << bid << ", ";
	}
	ss << "\n";
	QMessageBox msgBox;
	msgBox.setText(QString(ss.str().c_str()));
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setDefaultButton(QMessageBox::Ok);
	int ret = msgBox.exec();
}

void BakkesModInjectorCpp::ReleaseDLL()
{
	//BakkesModInstallation::overrideBakkesModFolder = "F:\\Bakkesmod\\development\\BakkesMod-rewrite\\Release\\";
	QMessageBox msgBox;
	msgBox.setText("Changed to release folder");
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setDefaultButton(QMessageBox::Ok);
	int ret = msgBox.exec();
}

void BakkesModInjectorCpp::DebugDLL()
{
	//BakkesModInstallation::overrideBakkesModFolder = "F:\\Bakkesmod\\development\\BakkesMod-rewrite\\Debug\\";
	QMessageBox msgBox;
	msgBox.setText("Changed to debug folder");
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setDefaultButton(QMessageBox::Ok);
	int ret = msgBox.exec();
}

bool BakkesModInjectorCpp::PopupRLRunningTillClosed()
{
	DWORD processId = dllInjector.GetProcessID64(RL_PROCESS_NAME);
	LOG_LINE(INFO, "Rocket League process id is " << processId)
	if (processId > 0)
	{
		LOG_LINE(INFO, "Prompting user for RL close")
		QMessageBox msgBox;
		std::stringstream ss;
		ss << "Rocket League needs to be closed for BakkesMod to update, would you like BakkesMod to exit Rocket League?" << std::endl;
		msgBox.setText(ss.str().c_str());
		msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
		msgBox.setDefaultButton(QMessageBox::Yes);
		int ret = msgBox.exec();
		if (ret == QMessageBox::Yes)
		{
			LOG_LINE(INFO, "User accepted RL close");
			int tries = 0;
			while (tries < 10)
			{
				LOG_LINE(INFO, "Found RocketLeague executable open, PID: " << dllInjector.GetProcessID64(RL_PROCESS_NAME) << ". Currently on try " << tries);
				processId = dllInjector.GetProcessID64(RL_PROCESS_NAME); //User might close process themselves in the mean time
				if (processId == 0) {
					LOG_LINE(INFO, "All instances of RL are now closed");
					return true;
				}
				HANDLE rlHandle;
				rlHandle = OpenProcess(PROCESS_ALL_ACCESS, false, processId);
				LOG_LINE(INFO, "Sending terminate");
				TerminateProcess(rlHandle, 1);
				LOG_LINE(INFO, "Waiting for Single Object (timeout=1000ms)");
				WaitForSingleObject(rlHandle, 1000);
				LOG_LINE(INFO, "Done waiting");
				CloseHandle(rlHandle);
				LOG_LINE(INFO, "Closed handle");
				tries++;
				
			}
			return false;
		}
		else {
			LOG_LINE(INFO, "User denied RL close")
			processId = dllInjector.GetProcessID64(RL_PROCESS_NAME); //User might close process themselves in the mean time
			if (processId == 0) {
				LOG_LINE(INFO, "User closed RL by themselves, returning true");
				return true;
			}
			LOG_LINE(INFO, "RL still running, returning false")
			return false;
		}
	}
	return true;
}

void BakkesModInjectorCpp::OnOpenBakkesModFolderClicked()
{
	auto rlPath = installation.GetBakkesModFolder();
	if (rlPath.empty())
	{
		QMessageBox msgBox;
		msgBox.setText("Could not find the BakkesMod folder");
		msgBox.setStandardButtons(QMessageBox::Ok);
		msgBox.setDefaultButton(QMessageBox::Ok);
		int ret = msgBox.exec();
		return;
	}
	//else
	//{
	//	QMessageBox msgBox;
	//	msgBox.setText("Should open " + QString(rlPath.c_str()));
	//	msgBox.setStandardButtons(QMessageBox::Ok);
	//	msgBox.setDefaultButton(QMessageBox::Ok);
	//	int ret = msgBox.exec();
	//}
	windowsUtils.OpenFolder(rlPath);

}

const std::string GetStateName(BakkesModStatus state)
{
	 #define X(state, name) name,
	static std::string state_names[] =
	{
		BAKKESMOD_STATES
	};
	#undef X
	return state_names[state];
}
