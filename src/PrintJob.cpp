//
// Created by lubos on 13.4.18.
//

#include "PrintJob.h"
#include <stdexcept>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

PrintJob::PrintJob(std::shared_ptr<Printer> printer, std::string_view fileName, const char* filePath)
: m_printer(printer), m_printerUniqueName(printer->uniqueName()), m_jobName(fileName)
{
	m_file.open(filePath);

	if (!m_file.is_open())
		throw std::runtime_error("Cannot open GCODE file");

	m_file.seekg(0, std::ios_base::end);
	m_size = m_file.tellg();
}

void PrintJob::start()
{
	if (m_state != State::Paused)
	{
		m_timeElapsed = std::chrono::seconds::zero();
		m_file.seekg(0, std::ios_base::beg);
	}
	else
	{
		std::shared_ptr<Printer> printer = m_printer.lock();

		if (printer)
		{
			// Move the extruder back
			printer->sendCommand("G0 Z-5", nullptr);

			// Restore the original positioning mode
			if (!m_positioningBeforePause.relativePositioning)
			{
				printer->sendCommand("G90", nullptr);
				if (m_positioningBeforePause.extruderRelativePositioning)
					printer->sendCommand("M83", nullptr);
			}
			else if (!m_positioningBeforePause.extruderRelativePositioning)
				printer->sendCommand("M82", nullptr);

			printer->sendCommand("M117 Job resumed", nullptr);
		}
	}

	setState(State::Running);
	m_startTime = std::chrono::steady_clock::now();
	printLine();
}

void PrintJob::stop()
{
	m_timeElapsed += std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_startTime);
	setState(State::Stopped);

	std::shared_ptr<Printer> printer = m_printer.lock();

	if (printer)
	{
		// Disable steppers
		printer->sendCommand("M18", nullptr);
		// Fan off
		printer->sendCommand("M107", nullptr);
		// Extruder heater off
		printer->sendCommand("M104 S0", nullptr);
		// Heatbed off
		printer->sendCommand("M140 S0", nullptr);

		// Move the extruder away
		printer->sendCommand("G91", nullptr); // relative movements
		printer->sendCommand("G0 Z10", nullptr); // up 1cm
	}
}

void PrintJob::pause()
{
	if (m_state != State::Running)
		return;

	m_timeElapsed += std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_startTime);

	setState(State::Paused);

	std::shared_ptr<Printer> printer = m_printer.lock();
	m_positioningBeforePause = printer->positioningState();

	// Pause sequence (move extruder away)
	if (!m_positioningBeforePause.relativePositioning)
		printer->sendCommand("G91", nullptr);
	printer->sendCommand("G0 Z5", nullptr);

	printer->sendCommand("M117 Job paused", nullptr);
}

void PrintJob::setError(std::string_view error)
{
	BOOST_LOG_TRIVIAL(error) << "Print job error on " << m_printerUniqueName << ": "
							 << error;

	std::unique_lock<std::mutex> lock(m_mutex);
	m_errorString = error;
	lock.unlock();

	setState(State::Error);
}

std::string PrintJob::nextLine()
{
	std::string line;

	do
	{
		std::getline(m_file, line);

		std::string::size_type pos = line.find(';');
		if (pos != std::string::npos)
			line.resize(pos);

		boost::algorithm::trim(line);
	}
	while (line.empty() && !m_file.eof() && !m_file.bad());

	m_position = m_file.tellg();
	if (m_file.eof())
		m_position = m_size;
	return line;
}

void PrintJob::printLine()
{
	std::shared_ptr<Printer> printer = m_printer.lock();

	try
	{
		std::string line = nextLine();

		if (!printer)
			throw std::runtime_error("The printer is gone");
		if (printer->state() != Printer::State::Connected)
			throw std::runtime_error("The printer is not connected");

		if (!line.empty())
		{
			// Send another line to the printer
			printer->sendCommand(line.c_str(), std::bind(&PrintJob::lineProcessed, this, std::placeholders::_1));
		}
		else
		{
			// Print job done
			m_timeElapsed += std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_startTime);

			setState(State::Done);
		}
	}
	catch (const std::exception& e)
	{
		setError(e.what());
		return;
	}
}

void PrintJob::lineProcessed(const std::vector<std::string>& resp)
{
	try
	{
		m_progressChangeSignal(m_position);

		if (m_state == State::Running)
			printLine();
	}
	catch (const std::exception &e)
	{
		setError(e.what());
	}
}

void PrintJob::setState(State state)
{
	if (state != m_state)
	{
		m_state = state;
		m_stateChangeSignal(state, m_errorString);
	}
}

std::string PrintJob::errorString() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_errorString;
}

const char* PrintJob::stateString() const
{
	return stateString(m_state);
}

const char* PrintJob::stateString(State state)
{
	switch (state)
	{
		case State::Stopped:
			return "Stopped";
		case State::Running:
			return "Running";
		case State::Done:
			return "Done";
		case State::Paused:
			return "Paused";
		case State::Error:
		default:
			return "Error";
	}
}

void PrintJob::progress(size_t& pos, size_t& total) const
{
	pos = (m_state == State::Done) ? m_size : m_position;
	total = m_size;
}

std::chrono::seconds PrintJob::timeElapsed() const
{
	auto s = m_timeElapsed;
	if (m_state == State::Running)
		s += std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_startTime);
	return s;
}
