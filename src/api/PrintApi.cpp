/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   WebRESTHandler.cpp
 * Author: lubos
 * 
 * Created on 28. března 2018, 0:15
 */

#include "PrintApi.h"
#include <sstream>
#include "web/web.h"
#include "nlohmann/json.hpp"
#include "PrinterDiscovery.h"
#include "PrinterManager.h"
#include "util.h"
#include "PrintJob.h"
#include "AuthManager.h"

namespace
{
	void restPrintersDiscover(WebRequest& req, WebResponse& resp)
	{
		std::vector<DiscoveredPrinter> printers;
		nlohmann::json result = nlohmann::json::array();
		
		PrinterDiscovery::enumerateAll(printers);
		
		for (const DiscoveredPrinter& p : printers)
		{
			result.push_back({
				{ "path", p.devicePath },
				{ "name", p.deviceName },
				{ "vendor", p.deviceVendor },
				{ "serial", p.deviceSerial }
			});
		}
		
		resp.send(result);
	}

	nlohmann::json jsonFillPrinter(std::shared_ptr<Printer> printer, bool isDefault)
	{
		return nlohmann::json {
				{"device_path", printer->devicePath()},
				{"baud_rate",   printer->baudRate()},
				{"stopped",     printer->state() == Printer::State::Stopped},
				//{"api_key",     printer->apiKey()},
				{"name",        printer->name()},
				{"default",     isDefault},
				{"connected",   printer->state() == Printer::State::Connected},
				{"width", printer->printArea().width},
				{"height", printer->printArea().height},
				{"depth", printer->printArea().depth},
				{"state", Printer::stateName(printer->state())},
				{"errorMessage", printer->errorMessage()}
		};
	}

	void restPrinter(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);
		std::string defaultPrinter = printerManager->defaultPrinter();
		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());

		if (!printer)
			throw WebErrors::not_found("Unknown printer");

		resp.send(jsonFillPrinter(printer, printer->name() == defaultPrinter));
	}

	void restPrinterReset(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);
		std::string defaultPrinter = printerManager->defaultPrinter();
		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());

		if (!printer)
			throw WebErrors::not_found("Unknown printer");

		if (printer->state() != Printer::State::Error)
			throw WebErrors::bad_request("The printer is not in error state");
		printer->resetPrinter();
		
		resp.send(WebResponse::http_status::no_content);
	}

	void restPrinters(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		nlohmann::json result = nlohmann::json::object();

		std::set<std::string> names = printerManager->printerNames();
		std::string defaultPrinter = printerManager->defaultPrinter();

		for (const std::string& name : names)
		{
			std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());

			if (!printer)
				continue;

			result[name] = jsonFillPrinter(printer, printer->uniqueName() == defaultPrinter);
		}

		resp.send(result);
	}

	void restDeletePrinter(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);
		if (!printerManager->deletePrinter(name.c_str()))
			throw WebErrors::not_found("Unknown printer");
		else
			resp.send(WebResponse::http_status::no_content);
	}

	void configurePrinterFromJson(nlohmann::json& data, Printer* printer, bool& makeDefault, bool startByDefault)
	{
		if (data["name"].is_string())
			printer->setName(data["name"].get<std::string>().c_str());

		if (data["device_path"].is_string())
			printer->setDevicePath(data["device_path"].get<std::string>().c_str());

		if (data["baud_rate"].is_number())
			printer->setBaudRate(data["baud_rate"].get<int>());

		if (data["default"].is_boolean() && data["default"].get<bool>())
			makeDefault = true;

		Printer::PrintArea area = printer->printArea();
		if (data["width"].is_number())
			area.width = data["width"].get<int>();
		if (data["height"].is_number())
			area.height = data["height"].get<int>();
		if (data["depth"].is_number())
			area.depth = data["depth"].get<int>();

		printer->setPrintArea(area);

		if (data["stopped"].is_boolean())
		{
			bool stop = data["stopped"].get<bool>();
			if (stop != (printer->state() == Printer::State::Stopped))
			{
				if (stop)
					printer->stop();
				else
					printer->start();
			}
		}
		else if (startByDefault)
			printer->start();
	}

	void restSetupNewPrinter(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::shared_ptr<Printer> printer = printerManager->newPrinter();
		nlohmann::json data = req.jsonRequest();
		bool makeDefault = false;

		if (data["name"].get<std::string>().empty() || data["device_path"].get<std::string>().empty() || data["baud_rate"].get<int>() < 1200)
			throw WebErrors::bad_request("Missing parameters");

		configurePrinterFromJson(data, printer.get(), makeDefault, true);
		printer->setUniqueName(urlSafeString(printer->name(), "printer").c_str());

		printerManager->addPrinter(printer);

		if (makeDefault)
			printerManager->setDefaultPrinter(printer->uniqueName());

		printerManager->saveSettings();

		std::stringstream ss;
		ss << req.baseUrl() << "/api/v1/printers/" << printer->uniqueName();

		resp.set(WebResponse::http_header::location, ss.str());
		resp.send(WebResponse::http_status::created);
	}

	void restSetupPrinter(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		bool newPrinter = false, makeDefault = false;
		std::string name = req.pathParam(1);

		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());
		nlohmann::json data = req.jsonRequest();

		if (!printer)
		{
			newPrinter = true;
			printer = printerManager->newPrinter();
			printer->setUniqueName(name.c_str());
		}

		configurePrinterFromJson(data, printer.get(), makeDefault, false);

		if (newPrinter)
			printerManager->addPrinter(printer);

		if (makeDefault)
			printerManager->setDefaultPrinter(name.c_str());

		printerManager->saveSettings();

		resp.send(WebResponse::http_status::no_content);
	}

	void restSubmitJob(WebRequest& req, WebResponse& resp, PrinterManager* printerManager, FileManager* fileManager)
	{
		// If there's a paused/running print job, report a conflict
		// else create a new PrintJob based on the file name passed and start it.
		nlohmann::json jreq = req.jsonRequest();

		if (!jreq["file"].is_string())
			throw WebErrors::bad_request("missing 'file' param");

		std::string printerName = req.pathParam(1);
		std::shared_ptr<Printer> printer = printerManager->printer(printerName.c_str());

		if (!printer)
			throw WebErrors::not_found("Printer not found");

		std::shared_ptr<PrintJob> printJob = printer->printJob();
		if (printJob && (printJob->state() == PrintJob::State::Paused || printJob->state() == PrintJob::State::Running))
		{
			resp.send(WebResponse::http_status::conflict);
			return;
		}

		std::string fileName = jreq["file"].get<std::string>();
		std::string filePath = fileManager->getFilePath(fileName);

		if (!boost::filesystem::is_regular_file(filePath))
			throw WebErrors::not_found(".gcode file not found");
		
		printJob = std::make_shared<PrintJob>(printer, fileName, filePath.c_str());
		printer->setPrintJob(printJob);

		// Unless state is Stopped, start the job
		if (!jreq["state"].is_string() || jreq["state"].get<std::string>() != "Stopped")
			printJob->start();

		resp.send(WebResponse::http_status::no_content);
	}

	void restModifyJob(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		nlohmann::json jreq = req.jsonRequest();
		std::string printerName = req.pathParam(1);
		std::shared_ptr<Printer> printer = printerManager->printer(printerName.c_str());

		if (!printer)
			throw WebErrors::not_found("Printer not found");

		std::shared_ptr<PrintJob> printJob = printer->printJob();
		if (!printJob)
			throw WebErrors::not_found("Print job not found");
		
		if (jreq["state"].is_string())
		{
			std::string stateValue = jreq["state"].get<std::string>();
			if (stateValue == "Stopped")
			{
				switch (printJob->state())
				{
					case PrintJob::State::Running:
					case PrintJob::State::Paused:
						printJob->stop();
						break;
					default:
						;
				}
			}
			else if (stateValue == "Paused")
			{
				if (printJob->state() == PrintJob::State::Running)
					printJob->pause();
			}
			else if (stateValue == "Running")
			{
				if (printJob->state() != PrintJob::State::Running)
					printJob->start();
			}
			else
				throw WebErrors::bad_request("Unrecognized 'state' value");
		}

		resp.send(WebResponse::http_status::no_content);
	}

	void restGetJob(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string printerName = req.pathParam(1);
		std::shared_ptr<Printer> printer = printerManager->printer(printerName.c_str());

		// TODO
		if (!printer)
			throw WebErrors::not_found("Printer not found");

		std::shared_ptr<PrintJob> printJob = printer->printJob();
		if (!printJob)
			throw WebErrors::not_found("Print job not found");

		nlohmann::json result = nlohmann::json::object();
		result["state"] = printJob->stateString();
		result["error"] = printJob->errorString();
		result["name"] = printJob->name();

		size_t pos, total;
		printJob->progress(pos, total);

		result["done"] = pos;
		result["total"] = total;
		result["elapsed"] = printJob->timeElapsed().count();

		resp.send(result);
	}

	void restGetPrinterTemperatures(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);

		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());
		if (!printer)
			throw WebErrors::not_found("Printer not found");

		nlohmann::json result = nlohmann::json::array();
		std::list<Printer::TemperaturePoint> temps = printer->getTemperatureHistory();

		for (auto it = temps.begin(); it != temps.end(); it++)
		{
			nlohmann::json values = nlohmann::json::object();

			for (auto it2 = it->values.begin(); it2 != it->values.end(); it2++)
			{
				values[it2->first] = {
						{"current", it2->second.current},
						{"target",  it2->second.target}
				};
			}

			std::time_t tt = std::chrono::system_clock::to_time_t(it->when);
			char when[sizeof "2011-10-08T07:07:09.000Z"];
			strftime(when, sizeof when, "%FT%T.000Z", gmtime(&tt));

			nlohmann::json pt = nlohmann::json::object();
			pt["when"] = when;
			pt["values"] = values;
			result.push_back(pt);
		}

		resp.send(result, WebResponse::http_status::ok);
	}

	void restSetPrinterTemperatures(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);

		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());
		if (!printer)
			throw WebErrors::not_found("Printer not found");

		if (printer->state() != Printer::State::Connected)
			throw WebErrors::bad_request("Printer not connected");

		nlohmann::json changes = req.jsonRequest();
		if (!changes.is_object())
			throw WebErrors::bad_request("JSON object expected");

		int commandsToGo = 0;
		for (nlohmann::json::iterator it = changes.begin(); it != changes.end(); it++)
		{
			std::string key = it.key();
			int temp = it.value().get<int>();
			std::string gcode;

			if (key == "T")
				gcode = "M104 S";
			else if (key == "B")
				gcode = "M140 S";
			else
				throw WebErrors::bad_request("Unknown temp target key");

			gcode += std::to_string(temp);

			commandsToGo++;
			printer->sendCommand(gcode.c_str(), [&](const std::vector<std::string>& reply) {
				// TODO: Are there ever any errors reported back by the printers here?

				commandsToGo--;
				if (commandsToGo == 0)
					resp.send(WebResponse::http_status::no_content);
			});
		}

		if (commandsToGo == 0)
			resp.send(WebResponse::http_status::no_content);
	}

	void restGetGcodeHistory(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);

		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());
		if (!printer)
			throw WebErrors::not_found("Printer not found");

		nlohmann::json result = nlohmann::json::array();
		std::list<Printer::GCodeEvent> gcodeHistory = printer->gcodeHistory();

		for (const auto& e : gcodeHistory)
		{
			nlohmann::json line = {
				{ "id", e.commandId },
				{ "outgoing", e.outgoing },
				{ "data", e.data }
			};

			if (!e.tag.empty())
				line["tag"] = e.tag;

			result.push_back(line);
		}

		resp.send(result);
	}

	void restSubmitGcode(WebRequest& req, WebResponse& resp, PrinterManager* printerManager)
	{
		std::string name = req.pathParam(1);

		std::shared_ptr<Printer> printer = printerManager->printer(name.c_str());

		if (!printer)
			throw WebErrors::not_found("Printer not found");
		if (printer->state() != Printer::State::Connected)
			throw WebErrors::bad_request("Printer not connected");

		if (req.hasRequestFile())
			throw WebErrors::bad_request("GCode data too long");
		
		if (req.request().body().length() > 200)
			throw WebErrors::bad_request("GCode data too long");
		if (req.request().body().length() == 0)
			throw WebErrors::bad_request("Missing GCode data");
		
		std::string line = req.request().body();

		if (line.find('\n') != std::string::npos)
			throw WebErrors::bad_request("Multiline GCode not accepted");
		
		auto invalidChar = std::find_if(line.begin(), line.end(), [](char c) {
			return c < 0x20 || c > 0x7e;
		});

		if (invalidChar != line.end())
			throw WebErrors::bad_request("Invalid characters encountered");

		std::string tag = AuthManager::generateSharedSecret(10);
		printer->sendCommand(line.c_str(), nullptr, tag);

		resp.send(nlohmann::json {
			{ "tag", tag }
		});
	}
}

void routePrinter(WebRouter* router, FileManager& fileManager, PrinterManager& printerManager)
{
	router->post("printers/discover", restPrintersDiscover);
	router->get("printers", restPrinters, &printerManager);
	router->post("printers", restSetupNewPrinter, &printerManager);
	
	router->put("printers/([^/]+)", restSetupPrinter, &printerManager);
	router->get("printers/([^/]+)", restPrinter, &printerManager);
	router->post("printers/([^/]+)/reset", restPrinterReset, &printerManager);
	router->delete_("printers/([^/]+)", restDeletePrinter, &printerManager);

	router->post("printers/([^/]+)/job", restSubmitJob, &printerManager, &fileManager);
	router->put("printers/([^/]+)/job", restModifyJob, &printerManager);
	router->get("printers/([^/]+)/job", restGetJob, &printerManager);

	router->get("printers/([^/]+)/gcode", restGetGcodeHistory, &printerManager);
	router->post("printers/([^/]+)/gcode", restSubmitGcode, &printerManager);

	router->put("printers/([^/]+)/temperatures", restSetPrinterTemperatures, &printerManager);
	router->get("printers/([^/]+)/temperatures", restGetPrinterTemperatures, &printerManager);
}
