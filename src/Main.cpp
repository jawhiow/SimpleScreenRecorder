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

#include "Global.h"

#include "Benchmark.h"
#include "CommandLineOptions.h"
#include "CPUFeatures.h"
#include "HotkeyListener.h"
#include "Icons.h"
#include "Logger.h"
#include "MainWindow.h"
#include "ScreenScaling.h"
#include "HTTPServer.h"
#include "PageRecord.h"

#include <signal.h>
#include <execinfo.h>
#include <QFileInfo>
#include <QDir>

// 信号处理函数
void SignalHandler(int signal) {
	// 生成堆栈跟踪
	const int max_frames = 100;
	void* stack_frames[max_frames];
	int frame_count = backtrace(stack_frames, max_frames);
	char** symbols = backtrace_symbols(stack_frames, frame_count);
	
	// 创建错误消息
	QString error_message = QString("Program received signal %1\n").arg(signal);
	error_message += "Stack trace:\n";
	
	// 添加堆栈帧
	for(int i = 0; i < frame_count; ++i) {
		error_message += QString("  %1\n").arg(symbols[i]);
	}
	
	// 记录错误
	if(Logger::GetInstance() != NULL) {
		Logger::LogError(error_message);
	} else {
		fprintf(stderr, "%s\n", error_message.toUtf8().constData());
	}
	
	// 释放符号字符串
	free(symbols);
	
	// 写入完成，退出
	exit(1);
}

// 设置信号处理程序
void SetupSignalHandlers() {
	signal(SIGSEGV, SignalHandler);  // 段错误
	signal(SIGABRT, SignalHandler);  // 异常终止
	signal(SIGFPE, SignalHandler);   // 浮点异常
	signal(SIGILL, SignalHandler);   // 非法指令
	signal(SIGTERM, SignalHandler);  // 终止信号
}

// 打印对象状态的调试函数
void DumpObjectState(MainWindow *mainwindow) {
	QString state = "Object State Dump:\n";
	
	// 检查MainWindow
	if(mainwindow == NULL) {
		state += "- MainWindow: NULL\n";
	} else {
		state += "- MainWindow: Valid\n";
		
		// 检查PageRecord
		PageRecord *page_record = mainwindow->GetPageRecord();
		if(page_record == NULL) {
			state += "- PageRecord: NULL\n";
		} else {
			state += "- PageRecord: Valid\n";
			state += QString("  - IsRecording: %1\n").arg(page_record->IsRecording() ? "yes" : "no");
			state += QString("  - IsPaused: %1\n").arg(page_record->IsPaused() ? "yes" : "no");
			state += QString("  - CurrentFileName: %1\n").arg(page_record->GetCurrentFileName());
		}
		
		// 检查PageInput
		PageInput *page_input = mainwindow->GetPageInput();
		if(page_input == NULL) {
			state += "- PageInput: NULL\n";
		} else {
			state += "- PageInput: Valid\n";
			state += QString("  - VideoBackend: %1\n").arg(page_input->GetVideoBackend());
			state += QString("  - AudioEnabled: %1\n").arg(page_input->GetAudioEnabled() ? "yes" : "no");
		}
		
		// 检查PageOutput - 避免使用前向声明类的方法
		PageOutput *page_output = mainwindow->GetPageOutput();
		if(page_output == NULL) {
			state += "- PageOutput: NULL\n";
		} else {
			state += "- PageOutput: Valid\n";
			// 不使用 GetFile() 方法，因为 PageOutput 是不完整类型
			// state += QString("  - File: %1\n").arg(page_output->GetFile());
		}
	}
	
	// 记录状态
	Logger::LogInfo(state);
}

int main(int argc, char* argv[]) {

	XInitThreads();

	// Workarounds for broken screen scaling.
	ScreenScalingFix();

	QApplication application(argc, argv);

	// SSR uses two separate character encodings:
	// - UTF-8: Used for all internal strings.
	//   Used by QString::fromAscii and QString::toAscii, and all implicit conversions from C-strings to QString. Also used for translations.
	// - Local character encoding: Used for file names and logs. In practice this will almost always be UTF-8 as well.
	//   Used by QString::fromLocal8Bit and QString::toLocal8Bit.
	// If it is not clear what encoding an external library uses, I use the local encoding for file names and UTF-8 for everything else.
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
	QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
#endif

	// set the application name
	QCoreApplication::setOrganizationName("SimpleScreenRecorder");
	QCoreApplication::setApplicationName("SimpleScreenRecorder");

	// load Qt translations
	QTranslator translator_qt;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	if(translator_qt.load(QLocale::system(), "qt", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
		QApplication::installTranslator(&translator_qt);
	}
#else
	if(translator_qt.load(QLocale::system(), "qt", "_", QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
		QApplication::installTranslator(&translator_qt);
	}
#endif

	// load SSR translations
	QTranslator translator_ssr;
	if(translator_ssr.load(QLocale::system(), "simplescreenrecorder", "_", QCoreApplication::applicationDirPath() + "/translations")) {
		QApplication::installTranslator(&translator_ssr);
	} else if(translator_ssr.load(QLocale::system(), "simplescreenrecorder", "_", GetApplicationSystemDir("translations"))) {
		QApplication::installTranslator(&translator_ssr);
	}

	// Qt doesn't count hidden windows, so if the main window is hidden and a dialog box is closed, Qt thinks the application should quit.
	// That's not what we want, so disable this and do it manually.
	QApplication::setQuitOnLastWindowClosed(false);

	// create logger
	Logger logger;
	Q_UNUSED(logger);

	// parse command line options
	CommandLineOptions command_line_options;
	try {
		command_line_options.Parse();
	} catch(const CommandLineException&) {
		return 1;
	}

	// do we need to continue?
	if(!CommandLineOptions::GetBenchmark() && !CommandLineOptions::GetGui() && !CommandLineOptions::GetBackend()) {
		return 0;
	}

	// configure the logger
	if(!CommandLineOptions::GetLogFile().isEmpty()) {
		logger.SetLogFile(CommandLineOptions::GetLogFile());
	}
	if(CommandLineOptions::GetRedirectStderr()) {
		logger.RedirectStderr();
	}

	// start main program
	Logger::LogInfo("==================== " + Logger::tr("SSR started") + " ====================");
	Logger::LogInfo(GetVersionInfo());

#if SSR_USE_X86_ASM
	// detect CPU features
	CPUFeatures::Detect();
#endif

	// show screen scaling message
	ScreenScalingMessage();

	// load icons
	LoadIcons();

	// 设置信号处理程序来捕获崩溃
	SetupSignalHandlers();

	// start the program
	int ret = 0;
	if(CommandLineOptions::GetBenchmark()) {
		Logger::LogInfo(Logger::tr("Starting benchmark ..."));
		
		Benchmark();
		
		return 0;
	}
	
	// backend mode?
	if(CommandLineOptions::GetStartRecording() || !CommandLineOptions::GetOutputFile().isEmpty()) {
		Logger::LogInfo(Logger::tr("Starting in backend mode ..."));
		Logger::LogInfo(Logger::tr("Output file: ") + CommandLineOptions::GetOutputFile());
		
		try {
			// create the main window hidden
			Logger::LogInfo(Logger::tr("Creating hidden main window ..."));
			MainWindow mainwindow(true);
			
			// dump initial state
			Logger::LogInfo(Logger::tr("Dumping initial object state ..."));
			DumpObjectState(&mainwindow);
			
			// get the right pages
			Logger::LogInfo(Logger::tr("Getting UI pages ..."));
			PageInput *pageinput = mainwindow.GetPageInput();
			if(pageinput == NULL) {
				Logger::LogError(Logger::tr("Error: PageInput is NULL!"));
				return 1;
			}
			
			PageOutput *pageoutput = mainwindow.GetPageOutput();
			if(pageoutput == NULL) {
				Logger::LogError(Logger::tr("Error: PageOutput is NULL!"));
				return 1;
			}
			
			PageRecord *pagerecord = mainwindow.GetPageRecord();
			if(pagerecord == NULL) {
				Logger::LogError(Logger::tr("Error: PageRecord is NULL!"));
				return 1;
			}
			
			// load settings
			Logger::LogInfo(Logger::tr("Loading settings ..."));
			mainwindow.LoadSettings();
			
			// set the output file if needed
			if(!CommandLineOptions::GetOutputFile().isEmpty()) {
				Logger::LogInfo(Logger::tr("Setting output file to: ") + CommandLineOptions::GetOutputFile());
				pageoutput->SetOutput(CommandLineOptions::GetOutputFile());
			}
			
			// start recording if needed
			if(CommandLineOptions::GetStartRecording()) {
				Logger::LogInfo(Logger::tr("Starting recording automatically ..."));
				try {
					if(!pagerecord->TryStartPage()) {
						Logger::LogError(Logger::tr("Failed to start recording!"));
						return 1;
					}
					Logger::LogInfo(Logger::tr("Recording started successfully."));
				} catch(const std::exception& e) {
					Logger::LogError(Logger::tr("Error starting recording: ") + e.what());
					return 1;
				}
			}
			
			// create HTTP server if in backend mode
			if(CommandLineOptions::GetBackend()) {
				Logger::LogInfo(Logger::tr("Starting HTTP server for backend mode ..."));
				try {
					HTTPServer server(pagerecord);
					server.Start(CommandLineOptions::GetHttpPort());
					Logger::LogInfo(Logger::tr("HTTP server started on port %1").arg(CommandLineOptions::GetHttpPort()));
					
					// start event loop
					return application.exec();
				} catch(const std::exception& e) {
					Logger::LogError(Logger::tr("HTTP server error: ") + e.what());
					return 1;
				}
			}
			
			// if we get here and no recording was started, just show the window
			if(!CommandLineOptions::GetStartRecording()) {
				Logger::LogInfo(Logger::tr("No recording started, showing main window."));
				mainwindow.show();
				return application.exec();
			}
			
			// if we get here, recording started but no HTTP server, so just run the app
			return application.exec();
			
		} catch(const std::exception& e) {
			Logger::LogError(Logger::tr("Backend error: ") + e.what());
			return 1;
		} catch(...) {
			Logger::LogError(Logger::tr("Unknown backend error!"));
			return 1;
		}
	}
	else if(CommandLineOptions::GetGui()) {
		// create hotkey listener
		HotkeyListener hotkey_listener;
		Q_UNUSED(hotkey_listener);

		// create main window
		MainWindow mainwindow;

		// run application
		ret = application.exec();
	}

	// stop main program
	Logger::LogInfo("==================== " + Logger::tr("SSR stopped") + " ====================");

	return ret;
}
