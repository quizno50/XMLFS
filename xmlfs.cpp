#include <Quizno50XML/Quizno50XML.hpp>
#include <Quizno50XML/FileString.hpp>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>
#include <cstring>
#include <mutex>

#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>

Document d;

class Log {
	public:
	enum LogLevel {
		DEBUG,
		INFO,
		WARNING,
		ERROR
	};

	Log(LogLevel l) : _message_level(l) {
		_iolock.lock();
	}

	~Log() {
		if (_message_level >= _log_level) {
			_out << _internalStream.str() << "\n";
		}
		_iolock.unlock();
	}

	operator std::ostream&() {
		return _internalStream;
	}

	template <class T>
	std::ostream& operator <<(T const& rhs) {
		return _internalStream << rhs;
	}

	protected:
	LogLevel _message_level;
	std::stringstream _internalStream;
	static LogLevel _log_level;
	static std::ostream& _out;
	static std::mutex _iolock;
};

Log::LogLevel Log::_log_level = Log::DEBUG;
std::ostream& Log::_out = std::cout;
std::mutex Log::_iolock;

char const* positionalArgument(int n, int argc, char const* const* argv) {
	int argidx = 1;
	char const* arg = *argv;
	while (n > 0 and argidx < argc) {
		arg = argv[argidx++];
		if (arg[0] != '-') {
			--n;
		}
	}
	return arg;
}

std::string longArgument(std::string argName, int argc, char** argv) {
	while (argc > 1) {
		--argc;
		std::string fullArg(argv[argc]);
		if (fullArg.substr(0, 2 + argName.length()) == ("--" + argName)) {
			size_t equalsAt = fullArg.find('=');
			if (equalsAt != std::string::npos) {
				return fullArg.substr(equalsAt + 1);
			} else {
				return argv[argc + 1];
			}
		}
	}
	return "";
}

class XPathSelectorOptions {
	public:
		XPathSelectorOptions() : _idx(1) { }
		XPathSelectorOptions(int idx) : _idx(idx) { }
		XPathSelectorOptions(std::string pe) : _idx(1) {
			if (pe[0] == '@') {
				off_t equals_at = pe.find('=');
				std::string attr = pe.substr(1, equals_at - 1);
				std::string val = pe.substr(equals_at + 1);
				_attr_match[attr] = val;
			} else if (pe[0] == '#') {
				_attr_match["id"] = pe.substr(1);
			} else if (pe[0] == '.') {
				_attr_match["class"] = pe.substr(1);
			} else if ((pe[0] > '0' and pe[0] < '9') or pe[0] == '-') {
				try {
				_idx = stoi(pe);
				} catch (std::invalid_argument const& e) {
					_idx = 1;
				}
			}
		}
		virtual ~XPathSelectorOptions() { }
		int getIdx() const {
			return _idx;
		}
		std::string getAttr(std::string attr) const {
			return _attr_match.at(attr);
		}
		bool hasAttr(std::string attr) const {
			return _attr_match.find(attr) != _attr_match.end();
		}
		bool attrsMatch(std::map<std::string,
				std::string>& attrs) const {
			for (auto const& attr : _attr_match) {
				try {
					if (attr.second != attrs.at(
							static_cast<std::string>(attr.first))) {
						return false;
					}
				} catch (...) {
					return false;
				}
			}
			return true;
		}
	protected:
		int _idx;
		bool _use_idx;
		std::map<std::string, std::string> _attr_match;
};

class TagNotFound : public std::runtime_error {
	public:
		TagNotFound() : std::runtime_error("Not Found") { }
};

Tag* first_normal_tag(std::vector<Tag>& tags) {
	for (auto& tag : tags) {
		if (tag.type == Tag::TAG_NORMAL) {
			return &tag;
		}
	}
	throw TagNotFound();
}

Tag* find_tag_with_options(std::vector<Tag>& tags,
		std::string const& tagName, XPathSelectorOptions const& xo) {
	for (auto& t : tags) {
		int match_count = 0;
		bool tag_matches = true;
		if (tagName != "") {
			tag_matches &= (tagName == static_cast<std::string>(*t.name));
		}
		if (!tag_matches) continue;
		tag_matches &= xo.attrsMatch(t.attributes);
		if (tag_matches) {
			++match_count;
			if (match_count == xo.getIdx() or 
					(match_count < 0 and
					match_count == (int)tags.size() + xo.getIdx()))
				return &t;
		}
	}
	Log(Log::DEBUG) << "Tag not found: " << tagName;
	throw TagNotFound();
}

Tag* tag_from_path(std::vector<Tag>& tags,
		std::filesystem::path const& path) {
	Tag* t = nullptr;
	for (auto const& pe : path) {
		if (pe.native() == "/") {
			continue;
		}
		off_t opts_idx = pe.native().find('[');
		std::string node_name = pe.native().substr(0, opts_idx);
		XPathSelectorOptions xo;

		if ((size_t)opts_idx != std::string::npos) {
			xo = XPathSelectorOptions(pe.native().substr(opts_idx + 1,
					pe.native().rfind(']') - (opts_idx + 1)));
		}
		if (t == nullptr) {
			t = find_tag_with_options(d.tags, node_name, xo);
		} else {
			t = find_tag_with_options(t->children, node_name, xo);
		}
		Log(Log::DEBUG) << "(tag_from_path) Descending to " << t->name;
	}
	if (t != nullptr) {
		Log(Log::DEBUG) << "Tag found for path: " << path;
	}
	return t;
}

size_t calculate_text_size(Tag const* start) {
	size_t result = 0;
	for (auto const& t: start->children) {
		switch (t.type) {
			case Tag::TAG_TEXT:
				result += static_cast<std::string>(*t.name).length();
				break;
			case Tag::TAG_NORMAL:
				result += calculate_text_size(&t);
				break;
			default:
				// pass
				break;
		}
	}
	return result;
}

extern "C" {

static int xmlfs_read(const char* path, char* buf, size_t size, off_t offset,
		fuse_file_info* fi) {
	try {
		Log(Log::INFO) << "Trying to read " << size << " bytes from " << path <<
				"(" << offset << ")";
		Tag* t = &(tag_from_path(d.tags,
				std::filesystem::path(path))->children[0]);
		if (t != nullptr) {
			size = std::min(size,
					static_cast<std::string>(*t->name).length() - offset);
			memcpy(buf, static_cast<std::string>(*t->name).substr(offset,
					size).c_str(), size);
		}
	} catch (TagNotFound const& e) {
		Log(Log::ERROR) << "(read) TagNotFound";
		return -ENOENT;
	} catch (std::out_of_range const& e) {
		Log(Log::ERROR) << "(read) Out of Range";
		return -ERANGE;
	}
	Log(Log::INFO) << "Read: " << size << " bytes from " << path << "("
			<< offset << ")";
	return size;
}

static int xmlfs_getattr(const char* path_in, struct stat* s,
		struct fuse_file_info* fi) {
	std::filesystem::path p(path_in);

	Log(Log::INFO) << "Trying to getattr on " << path_in;
	s->st_mode |= S_IRUSR | S_IXUSR;
	Tag* t = nullptr;
	try {
		t = tag_from_path(d.tags, p);
	} catch (std::out_of_range const& e) {
		Log(Log::ERROR) << "(getattr) Out of Range";
		return -ERANGE;
	} catch (TagNotFound const& e) {
		Log(Log::ERROR) << "(getattr) TagNotFound";
		return -ENOENT;
	}
	if (t != nullptr) {
		if (std::all_of(t->children.begin(), t->children.end(), [](Tag const& t) {
				return t.type == Tag::TAG_TEXT;})) {
			Log(Log::DEBUG) << "(getattr) " << path_in << " is a file.";
			s->st_mode |= S_IFREG;
			for (auto const& it : t->children) {
				s->st_size += (it.type == Tag::TAG_TEXT ?
						static_cast<std::string>(*it.name).length() : 0);
			}
		} else {
			Log(Log::DEBUG) << "(getattr) " << path_in << " is a directory.";
			s->st_mode |= S_IFDIR;
		}
	} else {
		Log(Log::DEBUG) << "(getattr) " << path_in << " is the root directory.";
		s->st_mode |= S_IFDIR;
	}
	return 0;
}

static int xmlfs_readdir(const char* path_in, void* buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info* fi,
		enum fuse_readdir_flags flags) {
	Tag* t = nullptr;
	try {
		t = tag_from_path(d.tags, path_in);
	} catch (std::out_of_range const& e) {
		return -ERANGE;
	} catch (TagNotFound const& e) {
		return -ENOENT;
	}
	
	for (auto const& st : (t == nullptr ? d.tags : t->children)) {
		std::string tagName = *st.name;
		struct stat s = {};
		auto const& id_attr = st.attributes.find("id");
		auto const& class_attr = st.attributes.find("class");

		if (st.type != Tag::TAG_NORMAL) continue;
		if (id_attr != st.attributes.end()) {
			tagName += "[#" + id_attr->second + "]";
		} else if (class_attr != st.attributes.end()) {
			tagName += "[." + class_attr->second + "]";
		}

		s.st_size = calculate_text_size(&st);
		s.st_mode = std::all_of(st.children.begin(), st.children.end(),
				[](Tag const& tag_in) -> bool {
				return tag_in.type == Tag::TAG_NORMAL; });
		filler(buf, tagName.c_str(), &s, 0, fuse_fill_dir_flags(0));
	}

	return 0;
}

static const struct fuse_operations xmlfs_oper = {
	/*
	.init = xmlfs_init,
	*/
	.getattr = xmlfs_getattr,
	.read = xmlfs_read,
	.readdir = xmlfs_readdir,
	/*
	.open = xmlfs_open,
	*/
};

int main(int argc, char** argv) {
	std::string mount_point = positionalArgument(1, argc, argv);
	std::string filename = longArgument("xml-file", argc, argv);
	bool foreground = std::find_if(argv, argv + argc,
			std::bind(std::equal_to<std::string>(), "-f",
			std::placeholders::_1)) != argv + argc;
	FileString fs(filename);
	size_t start = 0;

	parseDocument(fs, start, d);

	char ** fuse_args = new char*[2 + foreground];
	fuse_args[0] = argv[0];
	fuse_args[1] = strdup(mount_point.c_str());
	if (foreground) fuse_args[2] = strdup("-f");

	return fuse_main(2 + foreground, fuse_args, &xmlfs_oper, nullptr);
}

} //extern "C"

