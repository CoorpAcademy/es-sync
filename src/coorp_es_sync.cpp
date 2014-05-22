#include <stdarg.h>
#include <tr1/unordered_set>
#include <mongo/client/dbclient.h>
#include <mongo/client/dbclientcursor.h>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
#include <boost/timer/timer.hpp>
#include <boost/regex.hpp>
#include <curl/curl.h>

using namespace mongo;
namespace po = boost::program_options;

namespace ll {
	enum LOG_LEVEL { DEBUG = 0, INFO, WARNING, ERROR };
}
FILE *logfd = NULL;
int loglevel;

#define POST_UPDATES do {                                        \
						 post_updates(config, vm, query_buffer); \
						 sndbytes += query_buffer.size();        \
						 query_buffer.clear();                   \
						 msgcount++;                             \
					 } while(0);


void log(unsigned int level, const char *format, ...)
{
	char buff[1024];
	char timebuff[20];
	va_list args;
	char *levelstr;
	if (level >= loglevel)
	{
		switch(level)
		{
			case ll::DEBUG:
				levelstr = "DEBUG";
			break;
			case ll::INFO:
				levelstr = "INFO";
			break;
			case ll::WARNING:
				levelstr = "WARNING";
			break;
			case ll::ERROR:
				levelstr = "ERROR";
			break;
			default:
				levelstr = "UNKNOWN";
			break;
		}
		boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();
		struct tm t = boost::posix_time::to_tm(now);
		strftime(timebuff, 20, "%Y/%m/%d %H:%M:%S", &t);
		va_start(args, format);
		vsnprintf(buff, 1024, format, args);
		va_end(args);
		printf("[coorp_es_sync] [%s] [%s] %s\n", timebuff, levelstr, buff);
		if (logfd)
			fprintf(logfd, "[coorp_es_sync] [%s] [%s] %s\n", timebuff, levelstr, buff);
	}
	assert((level >= 0) && (level <= ll::ERROR));
}

std::string format_date(const boost::smatch &match)
{
	// Mongo's Date is a millisecond since epoch timestamp. To avoid having to
	// define date mappings in ES for every date field, we convert it to
	// the ES's default datetime format (YYYY/MM/DDTHH:MM:SS+00:00) so it will
	// dynamically map it to a date field.
	Date_t bdate(boost::lexical_cast<unsigned long long>(match[1].str()));
	struct tm td;
	bdate.toTm(&td);
	char timebuff[32];
	strftime(timebuff, 32, "\"%Y-%m-%dT%H:%M:%S+00:00\"", &td);
	return timebuff;
}

size_t post_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	std::string *buffer = (std::string *)userdata;
	buffer->append(ptr, size * nmemb);
	return size * nmemb;
}

void prepare_es_connection(boost::property_tree::ptree &config, po::variables_map &vm, CURL *curl, const char *uri)
{
	std::string esurl = vm.count("esurl")?vm["esurl"].as<std::string>():config.get<std::string>("sync.config.esurl");
	curl_easy_setopt(curl, CURLOPT_URL, esurl.append(uri).c_str());
	if (config.get<bool>("sync.config.validate_certificate", false))
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
	}else{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}
	std::string user = vm.count("esuser")?vm["esuser"].as<std::string>():config.get<std::string>("sync.config.esuser", "");
	std::string pwd = vm.count("espwd")?vm["espwd"].as<std::string>():config.get<std::string>("sync.config.espwd", "");
	if (!user.empty() && !pwd.empty())
	{
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
		curl_easy_setopt(curl, CURLOPT_USERNAME, user.c_str());
		curl_easy_setopt(curl, CURLOPT_PASSWORD, pwd.c_str());
	}
}

bool process_es_response(CURL *curl, boost::property_tree::ptree *response_body = NULL)
{
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, post_callback);
	std::string response;
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	CURLcode res = curl_easy_perform(curl);
	if(res != CURLE_OK)
	{
		log(ll::ERROR, "Post updates failed : %s", curl_easy_strerror(res));
		return false;
	}

	std::stringstream ss;
	boost::property_tree::ptree pt;
	boost::property_tree::ptree *pres = response_body?response_body:(&pt);
	ss << response;
	try {
		boost::property_tree::json_parser::read_json(ss, *pres);
	}catch(std::exception &e){
		log(ll::ERROR, "Cout not parse JSON answer : %s", e.what());
		log(ll::ERROR, "Actual response : %s", response.c_str());
		return false;
	}
	std::string err = pres->get<std::string>("error", "");
	if (!err.empty())
	{
		log(ll::ERROR, "%s", err.c_str());
		return false;
	}
	return true;
}

boost::property_tree::ptree get_documents(boost::property_tree::ptree &config, po::variables_map &vm, std::string uri, std::string query)
{
	boost::property_tree::ptree documents;
	// Send a GET request to ES, retrieve result as a property tree
	CURL *curl = curl_easy_init();
	if (curl)
	{
		prepare_es_connection(config, vm, curl, uri.c_str());
		// Including a body in a get request is not a standard behavior
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query.c_str());

		bool es_res = process_es_response(curl, &documents);

		curl_easy_cleanup(curl);
		if (!es_res)
			throw std::runtime_error("Documents fetching failed");
	}else{
		throw std::runtime_error("Cannot init curl");
	}
	return documents;
}

void post_updates(boost::property_tree::ptree &config, po::variables_map &vm, std::string &queries, const char *uri = "/_bulk")
{
	// Post our JSON request to ES. Here we just handle HTTP(S) stuff.
	log(ll::DEBUG, "\t\tPosting %u bytes of data", queries.size());
	CURL *curl = curl_easy_init();
	if (curl)
	{
		prepare_es_connection(config, vm, curl, uri);
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queries.c_str());

		bool es_res = process_es_response(curl);

		curl_easy_cleanup(curl);
		if (!es_res)
			throw std::runtime_error("Update failed");
	}else{
		throw std::runtime_error("Cannot init curl");
	}
}

void set_read_pref(boost::property_tree::ptree &config, po::variables_map &vm, Query &q)
{
	std::string pref;
	try {
		pref = vm.count("dbreadpref")?vm["dbreadpref"].as<std::string>():config.get<std::string>("dbreadpref");
	}catch(std::exception){
		return;
	}
	if (pref == "primary")
		q.readPref(mongo::ReadPreference_PrimaryPreferred, mongo::BSONArray());
	else if (pref == "secondary")
		q.readPref(mongo::ReadPreference_SecondaryPreferred, mongo::BSONArray());
	else if (pref == "nearest")
		q.readPref(mongo::ReadPreference_Nearest, mongo::BSONArray());
}

void process_sync(boost::property_tree::ptree &config, po::variables_map &vm, boost::property_tree::ptree::iterator config_instance,
		DBClientConnection &c, std::string synctype)
{
	std::string esindex = vm.count("esindex")?vm["esindex"].as<std::string>():config_instance->first;
	boost::property_tree::ptree collections = config.get_child("sync.collections");

	// First, check if the index exists and create it if necessary.
	boost::property_tree::ptree stats = get_documents(config, vm, "/_stats", "");
	if (!stats.count("indices") || !stats.get_child("indices").count(esindex))
	{
		log(ll::INFO, "\tCreatindex index %s", esindex.c_str());
		std::string mapping, uri;

		// ES cannot auto map nested fields, so we need to explicitly map them at index creation
		for (boost::property_tree::ptree::iterator col_it = collections.begin(); col_it != collections.end(); col_it++)
		{
			bool col_has_nested = false;
			bool first = true;
			for (boost::property_tree::ptree::iterator fields_it = col_it->second.begin(); fields_it != col_it->second.end(); fields_it++)
			{
				if (fields_it->first == "nested_field")
				{
					if (first && !mapping.empty())
						mapping += "} },\n";
					log(ll::DEBUG, "\t\tAdding nested field %s to mapping", fields_it->second.data().c_str());
					if (mapping.empty())
						mapping += "{ mappings : { \n";
					if (!col_has_nested)
						mapping += "\"" + col_it->first + "\" : { properties: { \n";
					if (!first)
						mapping += ", ";
					mapping += "\"" + fields_it->second.data() + "\" : { type: \"nested\" }\n";
					col_has_nested = true;
					first = false;
				}
			}
		}
		if (!mapping.empty()) mapping += "} } } }";

		uri += "/" + esindex;
		post_updates(config, vm, mapping, uri.c_str());
	}

	// Mongo's BSONObj json string is almost what we need to feed ES, but we need to rewrite
	// ObjectIDs and Dates.
	boost::regex re_objectid("\\{ \"\\$oid\" : (\"[0-9a-f]{24}\") \\}");
	boost::regex re_date("\\{ \"\\$date\" : ([0-9]+) \\}");
	// When the query's size reached this point, it will trigger data's POST
	int size_trigger = config.get<int>("sync.config.message_size_bulk_trigger", 1000000000);
	std::string query_buffer;
	query_buffer.reserve(size_trigger * 2);
	// Some accouting
	unsigned int msgcount = 0;
	unsigned long doccount = 0;
	unsigned long delcount = 0;
	unsigned long sndbytes = 0;

	for (boost::property_tree::ptree::iterator col_it = collections.begin(); col_it != collections.end(); col_it++)
	{
		log(ll::INFO, "\tSyncing collection %s", col_it->first.c_str());
		std::string nscol = (vm.count("dbname")?vm["dbname"].as<std::string>():config_instance->second.get<std::string>("dbname")) +
			"." + col_it->first;
		// Just get the requested fields
		BSONObjBuilder ob;
		for (boost::property_tree::ptree::iterator fields_it = col_it->second.begin(); fields_it != col_it->second.end(); fields_it++)
			if (fields_it->first == "field")
				ob.append(fields_it->second.data(), 1);
		BSONObj obj = ob.done();
		Query q;
		if (synctype == "full")
			q = BSONObj();
		else if (synctype == "incremental")
		{
			// TODO : we could manage multiple fields with a $or query
			std::string updatefield = col_it->second.get("update_field", "");
			if (!updatefield.empty())
			{
				int incremental_offset = config.get<int>("sync.config.incremental_offset", 3600);
				Date_t since((time(NULL) - incremental_offset) * 1000);
				q = BSON(updatefield.c_str() << BSON("$gt" << since));
			}else{
				log(ll::WARNING, "No udpate field defined for collection, aborting incremental update");
				continue;
			}
		}
		// If specified in config, set a read preference on our query
		set_read_pref(config_instance->second, vm, q);
		auto_ptr<DBClientCursor> res = c.query(nscol, q, 0, 0, &obj);
		if (!c.getLastError().empty())
			throw std::runtime_error(c.getLastError());
		int coldoccount = 0;
		while (res->more())
		{
			BSONObj doc = res->next();
			// Bulk operation for our document, just "index" (eg. upsert)
			query_buffer += "{ \"index\": { \"_index\": \"" + esindex + "\", \"_type\": \"" +
					col_it->first + "\", \"_id\": \"" + doc["_id"].OID().toString() + "\", \"_retry_on_conflict\" : 3} }\n";
			// Export document to JSON string and perform our rewriting
			query_buffer += boost::regex_replace(boost::regex_replace(doc.jsonString(), re_objectid, "\\1"), re_date, format_date) + "\n";
			if (query_buffer.size() >= size_trigger)
				POST_UPDATES;
			coldoccount++;
		}
		doccount += coldoccount;

		if (synctype == "full")
		{
			// Now proceed with removal
			delcount = 0;
			// First, get all doc ids on the Mongo side and fill the hash map
			std::tr1::unordered_set<std::string> mongoids;
			q = BSONObj();
			set_read_pref(config_instance->second, vm, q);
			BSONObj resfields = BSON("_id" << 1);
			std::auto_ptr<DBClientCursor> res = c.query(nscol, q, 0, 0, &resfields);
			if (!c.getLastError().empty())
				throw std::runtime_error(c.getLastError());
			while (res->more())
				mongoids.insert(res->next().getField("_id").OID().toString());
			log(ll::DEBUG, "\t\tFetched %u mongo docs", mongoids.size());

			std::string uri, query;
			uri += "/" + esindex + "/" + col_it->first + "/_search";
			int fetch_size = config.get<int>("sync.config.fetch_size", 10000);
			unsigned long from = 0;
			unsigned int returned = 0;
			// Then, fetch doc ids on Es side
			do {
				query.clear();
				query += "{ \"fields\": [], \"size\": " + boost::lexical_cast<std::string>(fetch_size) +
						", \"from\": " + boost::lexical_cast<std::string>(from) + " }";
				boost::property_tree::ptree fetch_result = get_documents(config, vm, uri, query);
				boost::property_tree::ptree esdocs = fetch_result.get_child("hits.hits");
				returned = 0;
				for (boost::property_tree::ptree::iterator esdoc_it = esdocs.begin(); esdoc_it != esdocs.end(); esdoc_it++)
				{
					// If found in ES, but not in mongo, add to delete list
					if (mongoids.find(esdoc_it->second.get<std::string>("_id")) == mongoids.end())
					{
						query_buffer += "{ \"delete\": { \"_index\": \"" + esindex + "\", \"_type\": \"" +
								col_it->first + "\", \"_id\": \"" + esdoc_it->second.get<std::string>("_id") + "\" } }\n";
						if (query_buffer.size() >= size_trigger)
							POST_UPDATES;
						delcount++;
					}
					returned++;
				}
				log(ll::DEBUG, "\t\tFetched %u ES docs", returned);
				from += returned;
			}while (returned == fetch_size);
		}
		log(ll::INFO, "\t\t%u documents to add/update, %u to remove", coldoccount, delcount);
	}
	// If we have any datas remaining (and we will), flush now.
	if (!query_buffer.empty())
		POST_UPDATES;
	log(ll::INFO, "\tDone. Sent %lu updates in %u queries (total %lu bytes)", doccount + delcount, msgcount, sndbytes);
}

int main(int argc, char **argv)
{
	int retcode = 0;
	try {
		boost::property_tree::ptree config;
		DBClientConnection c;
		po::options_description desc("Available options");
		desc.add_options()
				("help", "gimme some hints")
				("conf", po::value<std::string>()->default_value("config.xml"), "xml config file")
				("type", po::value<std::string>(), "sync type: incremental or full")
				("esurl", po::value<std::string>(), "override config: elastic search url (eg. https://...)")
				("esuser", po::value<std::string>(), "override config: elastic search username")
				("espwd", po::value<std::string>(), "override config: elastic search password")
				("esindex", po::value<std::string>(), "override config: elastic search index")
				("loglevel", po::value<int>(), "override config: log level")
				("dbhost", po::value<std::string>(), "override config: database host")
				("dbname", po::value<std::string>(), "override config: database name")
				("dbuser", po::value<std::string>(), "override config: database username")
				("dbpwd", po::value<std::string>(), "override config: database password")
				("dbreadpref", po::value<std::string>(), "override config: database readpref")
				;
		po::variables_map vm;
		try {
			po::store(po::parse_command_line(argc, argv, desc), vm);
			po::notify(vm);
		}catch(std::exception &e){
			log(ll::ERROR, "Cannot parse command line : %s", e.what());
			retcode = 1;
			goto cleanup;
		}
		if (vm.count("help") || !vm.count("type"))
		{
			std::cout << desc << std::endl;
			retcode = 1;
			goto cleanup;
		}
		std::string synctype = vm["type"].as<std::string>();
		if ((synctype != "full") && (synctype != "incremental"))
		{
			log(ll::ERROR, "Sync type should be either full or incremental");
			retcode = 1;
			goto cleanup;
		}
		if (!boost::filesystem::is_regular(vm["conf"].as<std::string>()))
		{
			log(ll::ERROR, "Path %s does not exist or is not a file", vm["conf"].as<std::string>().c_str());
			retcode = 1;
			goto cleanup;
		}
		try {
			boost::property_tree::read_xml(vm["conf"].as<std::string>(), config);
		}catch(std::exception &e){
			log(ll::ERROR, "Could not read config file : %s", e.what());
			retcode = 1;
			goto cleanup;
		}

		loglevel = vm.count("loglevel")?vm["loglevel"].as<int>():config.get<int>("sync.config.loglevel", 1);
		if (boost::filesystem::is_regular(config.get<std::string>("sync.config.logfile", "")))
			logfd = fopen(config.get<std::string>("sync.config.logfile").c_str(), "a");

		log(ll::INFO, "Starting %s sync", vm["type"].as<std::string>().c_str());

		curl_global_init(CURL_GLOBAL_DEFAULT);

		// Loop on instances. For each one, connect to db, fetch docs for each collection and push to ES
		boost::property_tree::ptree instances = config.get_child("sync.instances");
		for (boost::property_tree::ptree::iterator it = instances.begin(); it != instances.end(); it++)
		{
			log(ll::INFO, "Processing instance %s", vm.count("esindex")?vm["esindex"].as<std::string>().c_str():it->first.c_str());
			std::string err;
			if (!c.connect(vm.count("dbhost")?vm["dbhost"].as<std::string>():it->second.get<std::string>("dbhost"), err))
			{
				log(ll::ERROR, "Could not connect to db : %s", err.c_str());
				retcode = 1;
				goto cleanup;
			}
			std::string dbuser = vm.count("dbuser")?vm["dbuser"].as<std::string>():it->second.get<std::string>("dbuser");
			std::string dbpwd = vm.count("dbpwd")?vm["dbpwd"].as<std::string>():it->second.get<std::string>("dbpwd");
			if (!dbuser.empty() && !dbpwd.empty())
				if (!c.auth(vm.count("dbname")?vm["dbname"].as<std::string>():it->second.get<std::string>("dbname"), dbuser, dbpwd, err))
				{
					log(ll::ERROR, "Cannot auth with mongo server : %s", err.c_str());
					retcode = 1;
					goto cleanup;
				}
			process_sync(config, vm, it, c, synctype);
		}
	}catch(std::exception &e){
		log(ll::ERROR, "Got exception during import : %s", e.what());
		retcode = 1;
		goto cleanup;
	}
	log(ll::INFO, "All done.");

cleanup:
	curl_global_cleanup();
	if (logfd)
		fclose(logfd);

	return retcode;
}
