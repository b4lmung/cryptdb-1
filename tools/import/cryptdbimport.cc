/*
 * Author: ccarvalho
 * Jun 25 19:44:52 BRT 2013
 *
 */
#include <string>
#include <stdio.h>
#include <iostream>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <map>
#include <stdlib.h>
#include <typeinfo>
#include <pthread.h>

#include <getopt.h>

#include <list>
#include <algorithm>

#include <cdb_rewrite.hh>
#include <Connect.hh>
#include <field.h>
#include <cryptdbimport.hh>

//for future use
//static pthread_mutex_t __attribute__((unused))
//    _mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * TODO:FIXME:
 *
 * format_create_database_query() TODO:
 *
 * - 1 Eliminate comma hacks.
 * - 2 Make it generic and then get rid of format_insert_table_query().
 * - 3 In a future not so far, analyze other options like boost::spirit and others.
 */
static string
format_create_database_query(const string dbname,
        const string tablename, table_structure ts, tsMap_t& tsmap)
{
    assert(tsmap.size() > 0);

    ostringstream q;
    q << "CREATE TABLE IF NOT EXISTS " << dbname << "." << tablename << " (";

    vector<string>fieldVec;
    vector<string>optionsVec;

    int i = 0;
    for(tsIt_t it = tsmap.begin(); it != tsmap.end(); ++it, ++i)
    {
        tsVpIt_t vit = it->second.begin();
        pairKeys_t p0 = it->first;

        if(p0.first == "field")
        {
            string s = p0.second + " ";
            fieldVec.push_back(s);

            ++vit;
            if(vit != it->second.end())
            {
                //Type
                fieldVec.push_back(vit->second);
                fieldVec.push_back("_fg_");
            }

            ++vit;
            if(vit != it->second.end())
            {
                //Null
                if(vit->second == "NO")
                {
                    //HACK(ccarvalho) Removes previous ','
                    // 'NOT NULL' must not be preceded by such character.
                    if(fieldVec.back() == "_fg_"){
                        fieldVec.pop_back();
                    }

                    fieldVec.push_back(" NOT NULL");
                    fieldVec.push_back("_fg_");
                }
            }

            ++vit;
            if(vit != it->second.end())
            {
                //Key
                if(s == "PRI")
                {
                    s = " PRIMARY KEY(" + p0.second + ")";
                    fieldVec.push_back(vit->second); 
                    fieldVec.push_back("_fg_");
                }else if(s == "MUL")
                {
                    s = " MULTIPLE KEY(" + p0.second + ")";
                    fieldVec.push_back(vit->second); 
                    fieldVec.push_back("_fg_");
                }else if(s == "UNI")
                {
                    s = " UNIQUE KEY(" + p0.second + ")";
                    fieldVec.push_back(vit->second); 
                    fieldVec.push_back("_fg_");
                }
            }
            ++vit; 
            if(vit != it->second.end() && vit->second.size() > 0)
            {
                //HACK(ccarvalho) Removes previous ','
                // 'NOT NULL' must not be preceded by such character.
                if(fieldVec.back() == "_fg_"){
                    fieldVec.pop_back();
                }
                string tmp = "DEFAULT " + vit->second;
                fieldVec.push_back(tmp);
                fieldVec.push_back("_fg_");
            }

            ++vit; 
            if(vit != it->second.end() && vit->second.size() > 0)
            {
                if(fieldVec.back() == "_fg_"){
                    fieldVec.pop_back();
                }
                //Extra
                fieldVec.push_back(vit->second);
                fieldVec.push_back("_fg_");
            }

            ++vit; 
            if(vit != it->second.end() && vit->second.size() > 0)
            {
                //Comment
                fieldVec.push_back(vit->second);
                fieldVec.push_back("_fg_");
            }

        } else if(p0.first == "key")
        {
            //TODO: Extend to get all keys
        } else if(p0.first == "options")
        {
            // Engine
            ++vit; 
            if(vit != it->second.end())
            {
                assert(vit->second.size() > 0);
                string s = ") ENGINE=" + vit->second + ";";
                optionsVec.push_back(s);
            }
            //TODO: Extend to get all options
        }
    }

    for(uint i = 0; i < fieldVec.size(); ++i)
    {
        if(fieldVec.at(i) == "_fg_" && i != fieldVec.size()-1)
        {
            q << ", ";
            continue;
        }
        if(fieldVec.at(i) != "_fg_")
            q << fieldVec.at(i) << " ";

        if(i < fieldVec.size()-1 && i > 0)
            if(fieldVec.at(i) == "_fg_")
                q << ", ";
    }

    for(uint i = 0; i < optionsVec.size(); ++i)
    {
        q << optionsVec.at(i);
    }

    fieldVec.clear();
    optionsVec.clear();
    return q.str();
}

static string
format_insert_table_query(const string dbname,
        const string tablename, table_structure ts, table_data td)
{
    tdVec_t data = td.get_data();

    ostringstream q;
    q << "INSERT INTO " << dbname << "." << tablename << " VALUES(";
    for(tdVec_t::iterator it = data.begin(); it != data.end(); ++it)
    {
        char *p;
        strtol(it->second.c_str(), &p, 10);

        if(it->second.size() == 0)
        {
            q << "NULL";
        }else
        {
            if(*p)
                q << "'" << it->second << "'";
            else
                q << it->second;
        }
        if (it+1 == data.end())
            break;

        q << ", ";

    }
    q << ");";

    return q.str();
}
static bool
createEmptyDB(XMLParser& xml, Connect & conn, const string dbname, bool exec)
{
    string q = "CREATE DATABASE IF NOT EXISTS " + dbname + ";";
    //TODO: This is commented out because 'create database' is a working in progress 
    //in CryptDB core and may be available soon.
#if 0
    cout << q << endl;
    if(exec == true)
    {
        //TODO/FIXME: Use executeQuery() instead.
        // I am not using it because of 'Unexpected Error: unhandled sql command 36'
        DBResult * dbres;
        assert(conn.execute(q, dbres));
        if(!dbres)
            return false;
    }
#endif
    return true;
}


/**
 * Structure fields write out.
 */
bool
XMLParser::writeRIWO(const string& dbname, const string& tablename,
        Rewriter& r, Connect& conn, bool exec, table_structure& ts)
{
    assert(dbname.size() > 0);
    assert(tablename.size() > 0);

    tsMap_t tsmap = ts.get_data();
    string q = format_create_database_query(dbname, tablename, ts, tsmap);
    cout << q << endl;

    if(exec == true)
        return (bool)executeQuery(r, q, true);

    return true;
}


/**
 * Data fields write out.
 */
bool
XMLParser::writeRIWO(const string& dbname, const string& tablename,
        Rewriter& r, Connect& conn, bool exec, table_structure& ts, table_data& td)
{
    assert(dbname.size() != 0);
    assert(tablename.size() != 0);

    if(td.get_size() == 0)
        return false;

    string q = format_insert_table_query(dbname, tablename, ts, td);
    td.clear();

    cout << q << endl;

    if(exec == true)
        return (bool)executeQuery(r, q, true);

    return true;
}

static void
loadXmlStructure(XMLParser& xml, Connect & conn, Rewriter& r, bool exec, xmlNode *node)
{
    xmlNode *cur_node = NULL;
    for (cur_node = node; cur_node; cur_node = cur_node->next)
    {
        if (cur_node->type == XML_ELEMENT_NODE)
        {
            // TODO: use XML native types to avoid casting
            if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"database")))
            {
                // DATABASE
                xmlAttrPtr attr = cur_node->properties;
                xmlNode *ch = cur_node->xmlChildrenNode;
                string dbname = (char*)attr->children->content;

                // create database if not exists
                if(!ch->next && exec == true)
                {
                    if(createEmptyDB(xml, conn, dbname, exec) == false)
                    {
                        throw runtime_error(string("Error creating empty database: ") +
                                string(__PRETTY_FUNCTION__));
                    }
                }

                while(ch != NULL)
                {
                    table_structure ts;
                    table_data td;

                    // table_structure
                    if ((!xmlStrcmp(ch->name, (const xmlChar *)"table_structure")))
                    {
                        // Here we create if not exists
                        xmlAttrPtr attr2 = ch->properties;
                        string tablename = (char*)attr2->children->content;
                        xmlNode *ch2 = ch->xmlChildrenNode;
                        while(ch2 != NULL)
                        {
                            keyMNG mng;
                            ident_t ident = (char*)ch2->name;
                            while(ch2->properties != NULL)
                            {
                                prop_t prop = (char*)ch2->properties->name;

                                xmlAttrPtr attr3 = ch2->properties;
                                assert(attr3 != NULL);

                                value_t value = (char*)attr3->children->content;
                                ts.add(make_pair(ident, mng.getKey(value)), make_pair(prop, value));
                                ch2->properties = ch2->properties->next;
                            }
                            mng.unsetKey();
                            ch2 = ch2->next;
                        }
                        // Write out
                        if(xml.writeRIWO(dbname, tablename, r, conn, exec, ts) == false)
                        {
                            //TODO/FIXME: ignoring this error for while.
                            // throw is here in case we find such case.
                            //throw runtime_error(string("Parsing error ?! ") +
                            //    string(__PRETTY_FUNCTION__));
                        }
                        ts.clear();

                    // table_data
                    } else if ((!xmlStrcmp(ch->name, (const xmlChar *)"table_data")))
                    {
                        // No need to create table here
                        xmlAttrPtr attr2 = ch->properties;
                        string tablename = (char*)attr2->children->content;

                        xmlNode *ch2 = ch->xmlChildrenNode;
                        while(ch2 != NULL)
                        {
                            if((!xmlStrcmp(ch2->name, (const xmlChar *)"row")))
                            {
                                xmlNode *ch3 = ch2->xmlChildrenNode;
                                while(ch3 != NULL)
                                {
                                    while(ch3->properties != NULL)
                                    {
                                        xmlAttrPtr attr3 = ch3->properties;

                                        // ROW_VALUE
                                        unsigned char *_val = xmlNodeListGetString(xml.doc, ch3->xmlChildrenNode, 1);

                                        value_t value("");
                                        if(_val)
                                            value = (char*)_val;
                                        ident_t ident = (char*)attr3->children->content;
                                        td.add(ident, value);

                                        ch3->properties = ch3->properties->next;
                                    }
                                    ch3 = ch3->next;
                                }
                            }
                            ch2 = ch2->next;
                            // Write out
                            if(xml.writeRIWO(dbname, tablename, r, conn, exec, ts, td) == false)
                            {
                                //cout << "Info: " << dbname << "::" << tablename
                                //    << " has table_structure but table_data is empty." << endl;
                            }
                        }
                    }
                    ch = ch->next;
                }
            }
        }
        loadXmlStructure(xml, conn, r, exec, cur_node->children);
    }
}

static void do_display_help(const char *arg)
{
    cout << "CryptDBImport" << endl;
    cout << "Use: " << arg << " [OPTIONS]" << endl;
    cout << "OPTIONS are:" << endl;
    cout << "-u<username>: MySQL server username" << endl;
    cout << "-p<password>: MySQL server password" << endl;
    cout << "-n: Do not execute queries. Only show stdout." << endl;
    cout << "-s<file>: MySQL's .sql dump file, originated from \"mysqldump\" tool." << endl;
    cout << "To generate DB's dump file use mysqldump, e.g.:" << endl;
    cout << "$ mysqldump -u root -pletmein --all-databases --xml  --no-tablespaces --skip-comments --complete-insert" << endl;
    exit(0);
}

static void
do_init(XMLParser & xml, Connect & conn, Rewriter& r, bool exec, const char *filename)
{
    xmlDoc *doc = NULL;
    xmlNode *node = NULL;

    assert(filename != NULL);
    doc = xmlReadFile(filename, NULL, 0);
    assert(doc != NULL);


    xml.doc = doc;
    node = xmlDocGetRootElement(xml.doc);
    loadXmlStructure(xml, conn, r, exec, node);
    xmlFreeDoc(xml.doc);
}


int main(int argc, char **argv)
{
    int c, threads = 1, optind = 0;
    XMLParser xml;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"show", required_argument, 0, 's'},
        {"password", required_argument, 0, 'p'},
        {"user", required_argument, 0, 'u'},
        {"noexec", required_argument, 0, 'n'},
        {"threads", required_argument, 0, 't'},
        {NULL, 0, 0, 0},
    };

    string username("");
    string password("");
    bool exec = true;
    while(1)
    {
        c = getopt_long(argc, argv, "hs:p:u:t:n", long_options, &optind);
        if(c == -1)
            break;

        switch(c)
        {
            case 'h':
                do_display_help(argv[0]);
            case 's':
                {
                    ConnectionInfo ci("localhost", username, password);
                    Rewriter r(ci, "/var/lib/shadow-mysql", "cryptdbtest", false, true);
                    //TODO:FIXME: This instance is wrong. 
                    Connect conn("localhost", username, password, "cryptdbtest");
                    do_init(xml, conn, r, exec, optarg);
                }
                break;
            case 'p':
                password = optarg;
                break;
            case 'u':
                username = optarg;
                break;
            case 't':
                threads = atoi(optarg);
                (void)threads;
                break;
            case 'n':
                exec = false;
                break;
            case '?':
                break;
            default:
                break;
        }
    }

    return 0;
}
