// mpd-ynca - An MPD client to control Yamaha AV receivers
// Copyright (C) 2020  James Le Cuirot <chewi@gentoo.org>

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <errno.h>
#include <mpd/capabilities.h>
#include <mpd/client.h>
#include <mpd/idle.h>
#include <mpd/player.h>
#include <mpd/recv.h>
#include <mpd/status.h>
#include <mpd/sticker.h>
#include <string.h>

#define XSTR(s) STR(s)
#define STR(s) #s

using boost::asio::ip::tcp;
using boost::optional;
namespace po = boost::program_options;

class YncaClient {
public:
	YncaClient(const std::string &host, const int port);
	~YncaClient() = default;

	std::string get_command(const std::string &command);
	void put_command(const std::string &command);
	void with_connection(std::function<void ()> func);

private:
	boost::asio::io_service io_service;
	tcp::socket socket;
	tcp::resolver resolver;
	tcp::resolver::query resolver_query;
};

YncaClient::YncaClient(const std::string &host, const int port) :
	io_service(),
	socket(io_service),
	resolver(io_service),
	resolver_query(host, std::to_string(port))
{
}

std::string YncaClient::get_command(const std::string &command) {
	boost::asio::streambuf streambuf;
	std::size_t avail, read;

	// Drain input from the socket before sending.
	avail = socket.available();
	auto buffer = streambuf.prepare(avail);
	read = socket.read_some(buffer);
	streambuf.consume(read);

	boost::asio::write(socket, boost::asio::buffer(command + "\r\n"));
	std::string response;

	// Keep collecting input from the socket until it goes silent for
	// 200ms. We are not assuming that the first line received after
	// sending the command is the associated response.
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		avail = socket.available();
		if (avail == 0) break;
		auto buffer = streambuf.prepare(avail);
		read = socket.read_some(buffer);
		streambuf.commit(read);
		auto iter = boost::asio::buffers_begin(streambuf.data());
		response.append(iter, iter + read);
	}

	return response;
}

void YncaClient::put_command(const std::string &command) {
	// The YNCA documentation states that we should wait at least
	// 100ms after sending each command. In practise, I've found that
	// this is not enough but 200ms seems to be.
	boost::asio::write(socket, boost::asio::buffer(command + "\r\n"));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void YncaClient::with_connection(std::function<void ()> func) {
	try {
		tcp::resolver::iterator endpoints = resolver.resolve(resolver_query);
		boost::asio::connect(socket, endpoints);
		func();
	} catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
	}

	try { socket.close(); }
	catch(...) {}
}

void connect_loop(YncaClient &ynca, const std::string &input, const optional<std::string> &default_program) {
	struct mpd_status *status = NULL;
	enum mpd_state old_state, new_state;

	const struct mpd_audio_format *format;
	uint8_t	channels;

	static const std::string \
		on_cmd = "@MAIN:PWR=On", \
		sound_prog_cmd = "@MAIN:SOUNDPRG=", \
		straight_cmd = "@MAIN:STRAIGHT=On", \
		input_get_cmd = "@MAIN:INP=?";

	const std::string input_put_cmd = "@MAIN:INP=" + input;

	struct mpd_connection *conn = mpd_connection_new(NULL, 0, 0);
	bool sticker_support = false;

	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
		goto error;

	if (default_program) {
		if (!mpd_send_allowed_commands(conn))
			goto error;

		struct mpd_pair *cmd;

		while ((cmd = mpd_recv_command_pair(conn))) {
			if (strcmp(cmd->name, "command") == 0 && strcmp(cmd->value, "sticker") == 0)
				sticker_support = true;

			mpd_return_pair(conn, cmd);
		}

		if (!sticker_support)
			std::cerr \
				<< "Warning: Server lacks 'sticker' command, ignoring per-song sound programs." << std::endl \
				<< "         SQLite is not enabled in the build or 'sticker_file' is not set." << std::endl;
	}

	status = mpd_run_status(conn);

	if (!status)
		goto error;

	old_state = mpd_status_get_state(status);
	mpd_status_free(status);

	while(true) {
		enum mpd_idle idle = mpd_run_idle_mask(conn, MPD_IDLE_PLAYER);

		if (idle == 0 && mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
			goto error;

		status = mpd_run_status(conn);

		if (!status)
			goto error;

		new_state = mpd_status_get_state(status);
		format = mpd_status_get_audio_format(status);
		channels = format ? format->channels : 0;
		mpd_status_free(status);

		if (new_state == MPD_STATE_PLAY) {
			ynca.with_connection([&]() {
				if (old_state != MPD_STATE_PLAY) {
					// Power the receiver on and switch to the
					// configured input when playback starts.
					ynca.put_command(on_cmd);
					ynca.put_command(input_put_cmd);
				} else if (ynca.get_command(input_get_cmd).find(input_put_cmd + "\r\n") == std::string::npos) {
					// Stop playback if the input has changed.
					mpd_run_stop(conn);
					return;
				}

				if (default_program) {
					struct mpd_pair *sticker = NULL;

					if (sticker_support) {
						struct mpd_song *song = mpd_run_current_song(conn);
						mpd_connection_clear_error(conn);

						if (song != NULL) {
							if (mpd_send_sticker_get(conn, "song", mpd_song_get_uri(song), "ynca_program")) {
								sticker = mpd_recv_sticker(conn);
							} else {
								mpd_connection_clear_error(conn);
							}

							mpd_song_free(song);
						}
					}

					if (sticker != NULL) {
						// Set the sound program from a sticker.
						ynca.put_command(sound_prog_cmd + std::string(sticker->value));

						do {
							mpd_return_sticker(conn, sticker);
							sticker = mpd_recv_sticker(conn);
						} while (sticker != NULL);
					} else {
						if (channels > 2) {
							// Use STRAIGHT for a multi-channel song.
							ynca.put_command(straight_cmd);
						} else {
							// Otherwise use configured sound program.
							ynca.put_command(sound_prog_cmd + *default_program);
						}
					}
				}
			});
		}

		old_state = new_state;
	}

	mpd_connection_free(conn);
	return;

error:
	std::cerr << "MPD connection error: " << mpd_connection_get_error_message(conn) << std::endl;
	mpd_connection_free(conn);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	return connect_loop(ynca, input, default_program);
}

int main() {
	po::variables_map vm;
	po::options_description config("Configuration");
	optional<std::string> default_program;

	config.add_options()
		("host", po::value<std::string>(), "Yamaha AV hostname")
		("port", po::value<int>()->default_value(50000), "Yamaha AV port")
		("input", po::value<std::string>(), "Yamaha AV input for MPD")
		("default-program", po::value(&default_program), "Yamaha AV default sound program name")
		;

	const std::filesystem::path *cf = nullptr;
	std::filesystem::path cf2;

	const auto *home = std::getenv("HOME");
	const auto *xdg_config = std::getenv("XDG_CONFIG_HOME");

	if (xdg_config) {
		cf2 = std::filesystem::path(xdg_config);
	} else {
		cf2 = std::filesystem::path(home) / ".config";
	}

	cf2 = cf2 / "mpd" / "ynca.conf";
	if (std::filesystem::exists(cf2)) cf = &cf2;

	if (!cf && home) {
		cf2 = std::filesystem::path(home) / ".mpd-ynca.conf";
		if (std::filesystem::exists(cf2)) cf = &cf2;
	}

	if (!cf) {
		cf2 = std::filesystem::path(XSTR(SYSCONFDIR)) / "mpd-ynca.conf";
		if (std::filesystem::exists(cf2)) cf = &cf2;
	}

	if (!cf) {
		std::cerr << "Could not find a configuration file." << std::endl;
		return EXIT_FAILURE;
	}

	std::ifstream ifs(*cf);

	if (!ifs) {
		std::cerr << "Could not open " << *cf << ": " << strerror(errno) << std::endl;
		return EXIT_FAILURE;
	}

	store(parse_config_file(ifs, config), vm);
	notify(vm);

	if (!vm.count("host")) {
		std::cerr << "host not set in " << *cf << "." << std::endl;
		return EXIT_FAILURE;
	}

	if (!vm.count("input")) {
		std::cerr << "input not set in " << *cf << "." << std::endl;
		return EXIT_FAILURE;
	}

	YncaClient ynca = YncaClient(
		vm["host"].as<std::string>(),
		vm["port"].as<int>()
	);

	connect_loop(
		ynca,
		vm["input"].as<std::string>(),
		default_program
	);

	return EXIT_FAILURE;
}
