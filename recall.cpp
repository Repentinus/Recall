/*
	Recall – a shot at regenerating partially forgotten passphrases
	Copyright © 2015 Repentinus <repentinus at fsfe plus recall dot org>
	
	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation, version 2.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <iostream>
#include <cstring>
#include <sstream>
#include <atomic>
#include <cerrno>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include <sys/mman.h>
#include <signal.h>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/locale.hpp>

using namespace std;
using namespace boost::locale;
using namespace boost::program_options;

#ifndef LC_PATH
#define LC_PATH "."
#endif
#ifndef LC_DOMAIN
#define LC_DOMAIN "Recall"
#endif

namespace {
	atomic_int gather_input;
	struct sigaction* default_handler;
	struct sigaction ng_handler;
	struct sigaction lg_handler;
}

unique_ptr <vector <set <string> > > get_input();
unique_ptr <vector <string> > obtain_matches(
	vector <set <string> > const&, bool (*)(string const&, string const&), bool, string const&);
bool test(string const&, string const&);
extern "C" {
	void next_group_handler(int);
	void last_group_handler(int);
}

int main(int const argc, char const* const* const argv)
{
	generator gen;
	gen.add_messages_path(LC_PATH);
	gen.add_messages_domain(LC_DOMAIN);
	locale::global(gen(""));
	cout.imbue(locale());

	cout << translate(
		"Recall – a shot at regenerating partially forgotten passphrases\n"
		"Copyright © 2015 Repentinus <repentinus at fsfe plus recall dot org>\n"
		"Licensed under GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>\n"
	) << endl;

	options_description cmd_description(translate("Recognized options"));
	bool return_on_first = false;
	string key_id;
	cmd_description.add_options()
		("help", gettext("produce this message"))
		("return-on-first, 1", value<bool>(&return_on_first)->default_value(true), gettext("return on first match"))
		("key-id", value<string>(&key_id)->default_value(""), gettext("key to be tested"))
	;
	variables_map vm;
	store(command_line_parser(argc, argv).options(cmd_description).run(), vm);
	notify(vm);

	if (vm.count("help")) {
		cout << cmd_description << endl;
		return 0;
	}

	if (mlockall(MCL_FUTURE | MCL_CURRENT)) {
		cerr << "TERMINAL:" << " " << strerror(errno) << endl;
		exit(EXIT_FAILURE);
	}

	cout << "Gathering input…" << endl;
	unique_ptr <vector <set <string> > > fragments = get_input();

	cout << "Wrapping up…" << endl;
	unique_ptr <vector <string> > matches = obtain_matches(*fragments, test, return_on_first, key_id);

	cout << "The following matches were found:" << endl;
	for (auto &x : *matches) {
		cout << x << endl;
	}

	return 0;
}

void last_group_handler(int)
{
	gather_input = 0;
	sigaction(SIGINT, default_handler, nullptr);
	cout << endl;
}

void next_group_handler(int)
{
	gather_input = 1;
	sigaction(SIGINT, &lg_handler, nullptr);
	cout << endl;
}

unique_ptr <vector <set <string> > > get_input()
{
	unique_ptr<vector <set <string> > > fragments = make_unique<vector <set <string> > >();

	ng_handler.sa_handler = next_group_handler;
	lg_handler.sa_handler = last_group_handler;
	if (sigaction(SIGINT, &lg_handler, default_handler)) {
		cerr << "TERMINAL:" << " " << strerror(errno) << endl;
		exit(EXIT_FAILURE);
	}
	
	sigset_t sigterm;
	sigemptyset(&sigterm);
	sigaddset(&sigterm, SIGTERM);
	gather_input = 1;
	
	while (gather_input) {
		if (gather_input == 1) {
			fragments->push_back(set <string>());
		}
		string s;
		getline(cin, s);

		if (sigprocmask(SIG_BLOCK, &sigterm, nullptr)) {
			cerr << "TERMINAL:" << " " << strerror(errno) << endl;
			exit(EXIT_FAILURE);
		}
		if (cin.fail()) {
			cin.clear();
		} else if (s.size()) {
			fragments->back().insert(s);
			if (gather_input == 1) {
				gather_input = 2;
				sigaction(SIGINT, &ng_handler, nullptr);
			}
		}
		if (sigprocmask(SIG_UNBLOCK, &sigterm, nullptr)) {
			cerr << "TERMINAL:" << " " << strerror(errno) << endl;
			exit(EXIT_FAILURE);
		}
	}

	return fragments;
}

unique_ptr <vector <string> > obtain_matches(
	vector <set <string> > const& fragments, bool (*test)(string const&, string const&), bool return_on_first, string const& key_id)
{
	unique_ptr <vector <string> > matches = make_unique<vector <string> >();

	vector <vector <set <string>::iterator> > S;
	for (auto i = fragments.begin(); i != fragments.end(); ++i) {
		if (!i->empty()) {
			S.push_back(vector <set <string>::iterator> {i->begin(), i->begin(), i->end()});
		}
	}
	
	while(S.size() && S.front()[0] != S.front()[2]) {
		stringstream ss;
		for (auto i = S.begin(); i != S.end(); ++i) {
			ss << *((*i)[0]);
		}
		if (test(ss.str(), key_id)) {
			matches->push_back(ss.str());
			if (return_on_first) {
				return matches;
			}
		}

		auto i = --S.end();
		for (++((*i)[0]); (*i)[0] == (*i)[2];) {
			if (i == S.begin()) {
				break;
			}
			(*i)[0] = (*i)[1];
			++((*(--i))[0]);
		}
	}

	return matches;
}

bool test(string const& s, string const& key_id)
{
	string t = boost::str(boost::format("echo \"%1%\" | gpg -q --passphrase-fd 0 --output /dev/null --yes --local-user %2%  --pinentry-mode loopback --sign /dev/null") % s % key_id);
	return !system(t.c_str());
}
