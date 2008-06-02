// ------------------------------------------------------------------------
// Pion is a development platform for building Reactors that process Events
// ------------------------------------------------------------------------
// Copyright (C) 2007-2008 Atomic Labs, Inc.  (http://www.atomiclabs.com)
//
// Pion is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// Pion is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
// more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with Pion.  If not, see <http://www.gnu.org/licenses/>.
//

#include <pion/PionConfig.hpp>
#include <pion/PionPlugin.hpp>
#include <pion/platform/PluginConfig.hpp>
#include <pion/platform/Codec.hpp>
#include <pion/platform/CodecFactory.hpp>
#include <pion/PionUnitTestDefs.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/mpl/list.hpp>
#include <boost/bind.hpp>
#include <fstream>

using namespace pion;
using namespace pion::platform;


/// external functions defined in PionPlatformUnitTests.cpp
extern const std::string& get_log_file_dir(void);
extern const std::string& get_config_file_dir(void);
extern const std::string& get_vocabulary_path(void);
extern const std::string& get_vocabularies_file(void);
extern void setup_logging_for_unit_tests(void);
extern void setup_plugins_directory(void);
extern void cleanup_vocab_config_files(void);


/// static strings used by these unit tests
static const std::string COMMON_LOG_FILE(get_log_file_dir() + "common.log");
static const std::string COMBINED_LOG_FILE(get_log_file_dir() + "combined.log");
static const std::string EXTENDED_LOG_FILE(get_log_file_dir() + "extended.log");
static const std::string CODECS_TEMPLATE_FILE(get_config_file_dir() + "codecs.tmpl");
static const std::string CODECS_CONFIG_FILE(get_config_file_dir() + "codecs.xml");


/// cleans up config files relevant to Codecs in the working directory
void cleanup_codec_config_files(bool copy_codec_config_file)
{
	cleanup_vocab_config_files();

	if (boost::filesystem::exists(CODECS_CONFIG_FILE))
		boost::filesystem::remove(CODECS_CONFIG_FILE);
	if (copy_codec_config_file)
		boost::filesystem::copy_file(CODECS_TEMPLATE_FILE, CODECS_CONFIG_FILE);
}


class PluginPtrReadyToAddCodec_F : public PionPluginPtr<Codec> {
public:
	PluginPtrReadyToAddCodec_F() {
		setup_logging_for_unit_tests();
		setup_plugins_directory();
	}
};

BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(PluginPtrReadyToAddCodec_S, 
									   boost::mpl::list<PluginPtrReadyToAddCodec_F>)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkOpenLogCodec) {
	BOOST_CHECK_NO_THROW(F::open("LogCodec"));
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkOpenJSONCodec) {
	BOOST_CHECK_NO_THROW(F::open("JSONCodec"));
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkOpenXMLCodec) {
	BOOST_CHECK_NO_THROW(F::open("XMLCodec"));
}

BOOST_AUTO_TEST_SUITE_END()


template<const char* plugin_name>
class PluginPtrWithCodecLoaded_F : public PluginPtrReadyToAddCodec_F {
public:
	PluginPtrWithCodecLoaded_F() {
		m_plugin_name = plugin_name;
		open(m_plugin_name);
		m_codec = NULL;
	}
	~PluginPtrWithCodecLoaded_F() {
		if (m_codec) destroy(m_codec);
	}

	Codec* m_codec;
	std::string m_plugin_name;
};

// These have external linkage so they can be used as template parameters.
extern const char LogCodec_name[]  = "LogCodec";
extern const char JSONCodec_name[] = "JSONCodec";
extern const char XMLCodec_name[]  = "XMLCodec";

typedef boost::mpl::list<PluginPtrWithCodecLoaded_F<LogCodec_name>,
						 PluginPtrWithCodecLoaded_F<JSONCodec_name>,
						 PluginPtrWithCodecLoaded_F<XMLCodec_name> > codec_fixture_list;

// PluginPtrWithCodecLoaded_S contains tests that should pass for any type of Codec
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(PluginPtrWithCodecLoaded_S, codec_fixture_list)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkIsOpenReturnsTrue) {
	BOOST_CHECK(F::is_open());
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetPluginNameReturnsPluginName) {
	BOOST_CHECK_EQUAL(F::getPluginName(), F::m_plugin_name);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkCreateReturnsSomething) {
	BOOST_CHECK((F::m_codec = F::create()) != NULL);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkDestroyDoesntThrowExceptionAfterCreate) {
	F::m_codec = F::create();
	BOOST_CHECK_NO_THROW(F::destroy(F::m_codec));
	F::m_codec = NULL;
}

BOOST_AUTO_TEST_SUITE_END()


typedef enum { CREATED, CLONED, MANUFACTURED } LINEAGE;

template<const char* plugin_type, LINEAGE lineage>
class CodecPtr_F {
public:
	CodecPtr_F() : m_config_ptr(NULL) {
		setup_logging_for_unit_tests();
		setup_plugins_directory();
		cleanup_codec_config_files(true);
		BOOST_REQUIRE(lineage == CREATED || lineage == CLONED || lineage == MANUFACTURED);
		if (lineage == MANUFACTURED) {
			p.reset(); // MANUFACTURED is only allowed for derived classes that support it.  See checkLineageIsOK.
		} else {
			m_ppp.open(plugin_type);
			m_original_codec_ptr = CodecPtr(m_ppp.create());
			BOOST_REQUIRE(lineage == CREATED || lineage == CLONED);
			p = (lineage == CREATED? m_original_codec_ptr : m_original_codec_ptr->clone());
			BOOST_REQUIRE(p);
		}
		m_codec_type = plugin_type;
	}
	~CodecPtr_F() {
		if (m_config_ptr) {
			xmlFreeNodeList(m_config_ptr);
		}
	}

	// From a string representation of a Codec configuration, obtain an xmlNodePtr that
	// points to a list of all the child nodes, as needed by Codec::setConfig().
	void parseConfig(const std::string& config_str, xmlNodePtr& config_ptr) {
		xmlDocPtr doc_ptr = xmlParseMemory(config_str.c_str(), config_str.size());
		BOOST_REQUIRE(doc_ptr);
		config_ptr = xmlDocGetRootElement(doc_ptr)->children;
		BOOST_REQUIRE(config_ptr);
	}

	CodecPtr p; // This is what's actually playing the role of fixture, i.e., F::p is being tested, not F itself.
	xmlNodePtr m_config_ptr;

	// If you feel the need to use this in a test, you should probably instead move the test to a more specific test suite.
	// This is here to make it easy to temporarily skip tests that belong here, but don't pass yet.
	std::string m_codec_type;

protected:
	CodecPtr m_original_codec_ptr;
	PionPluginPtr<Codec> m_ppp;
};

#define SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS \
	if (F::m_codec_type == "JSONCodec") { \
		BOOST_WARN_MESSAGE(false, "Skipping this test for JSONCodec fixture because JSONCodec is incomplete."); \
		return; \
	} \
	if (F::m_codec_type == "XMLCodec") { \
		BOOST_WARN_MESSAGE(false, "Skipping this test for XMLCodec fixture because XMLCodec is incomplete."); \
		return; \
	} 

typedef boost::mpl::list<
	CodecPtr_F<LogCodec_name, CREATED>,
	CodecPtr_F<LogCodec_name, CLONED>,
	CodecPtr_F<JSONCodec_name, CREATED>,
	CodecPtr_F<JSONCodec_name, CLONED>,
	CodecPtr_F<XMLCodec_name, CREATED>,
	CodecPtr_F<XMLCodec_name, CLONED>
> CodecPtr_fixture_list;

// CodecPtr_S contains tests that should pass for any type of Codec
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecPtr_S, CodecPtr_fixture_list)

// This will fail if the fixture template is instantiated with a lineage inappropriate for this test suite, e.g. MANUFACTURED.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkLineageIsOK) {
	BOOST_REQUIRE(F::p);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetContentType) {
	// Exact values are tested elsewhere, in tests of specific Codecs.
	BOOST_CHECK(F::p->getContentType() != "");
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetEventType) {
	BOOST_CHECK_EQUAL(F::p->getEventType(), Vocabulary::UNDEFINED_TERM_REF);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetId) {
	// Would it be better if this threw an exception?
	BOOST_CHECK(F::p->getId() == "");
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadWithEventOfUndefinedType) {
	EventFactory event_factory;
	EventPtr ep(event_factory.create(Vocabulary::UNDEFINED_TERM_REF));
	std::stringstream ss("some text\n");

	// Currently, this is returning true for LogCodecs.  Although a case can be made for this,
	// in that it's succeeding in reading zero fields, it seems misleading.
	// Should this throw an exception instead, e.g., something like EmptyFieldMap?
	// Should it return false, since it didn't read anything?
	// Or is it OK?
	BOOST_WARN(F::p->read(ss, *ep) == false);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadWithEventOfUndefinedTypeAndEmptyString) {
	EventFactory event_factory;
	EventPtr ep(event_factory.create(Vocabulary::UNDEFINED_TERM_REF));
	std::stringstream ss("");

	// See comments in previous test, checkReadWithEventOfUndefinedType.
	BOOST_CHECK(F::p->read(ss, *ep) == false);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadWithEventOfWrongType) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	VocabularyManager vocab_mgr;
	vocab_mgr.setConfigFile(get_vocabularies_file());
	vocab_mgr.openConfigFile();
	Event::EventType some_type = vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#useragent");

	EventFactory event_factory;
	EventPtr ep(event_factory.create(some_type));
	std::stringstream ss("some text\n");
	BOOST_CHECK_THROW(F::p->read(ss, *ep), Codec::WrongEventTypeException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetConfig) {
	// Prepare some valid input for Codec::setConfig().
	std::string event_type_1 = "urn:vocab:clickstream#http-request";
	parseConfig("<Codec>"
					"<EventType>" + event_type_1 + "</EventType>"
				"</Codec>",
				F::m_config_ptr);
	VocabularyManager vocab_mgr;
	vocab_mgr.setConfigFile(get_vocabularies_file());
	vocab_mgr.openConfigFile();

	// Confirm that setConfig() returns.
	BOOST_CHECK_NO_THROW(F::p->setConfig(vocab_mgr.getVocabulary(), F::m_config_ptr));

	// Check that Codec::getEventType() returns the EventType specified in the configuration.
	Event::EventType event_type_ref = vocab_mgr.getVocabulary().findTerm(event_type_1);
	BOOST_CHECK_EQUAL(F::p->getEventType(), event_type_ref);
}

// This is just one basic test of Codec::clone(), which is primarily being tested via fixtures
// CodecPtr_F<*, CLONED>, ConfiguredCodecPtr_F<*, CLONED>, etc.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkClone) {
	// Note that p might already be a clone, depending on the fixture.
	BOOST_CHECK(F::p->clone());

	// just one simple check of 'cloneness'
	BOOST_CHECK(F::p->clone()->getContentType() == F::p->getContentType());
}

BOOST_AUTO_TEST_SUITE_END()


template<const char* plugin_type, LINEAGE lineage>
class ConfiguredCodecPtr_F : public CodecPtr_F<plugin_type, lineage> {
public:
	ConfiguredCodecPtr_F() : NAME_1("Test Codec"),
							 EVENT_TYPE_1("urn:vocab:clickstream#http-request"),
							 FIELD_TERM_1("urn:vocab:clickstream#bytes"),
							 FIELD_NAME_1("bytes")
	{
		// Prepare a valid Codec configuration string.
		parseConfig("<Codec>"
						"<Plugin>" + std::string(plugin_type) + "</Plugin>"
						"<Name>" + NAME_1 + "</Name>"
						"<EventType>" + EVENT_TYPE_1 + "</EventType>"
						"<Field term=\"" + FIELD_TERM_1 + "\">" + FIELD_NAME_1 + "</Field>"
					"</Codec>",
					this->m_config_ptr);

		// Initialize the VocabularyManager.
		m_vocab_mgr.setConfigFile(get_vocabularies_file());
		m_vocab_mgr.openConfigFile();

		// Make a configured CodecPtr of the specified lineage.
		if (lineage == MANUFACTURED) {
			CodecFactory factory(m_vocab_mgr);
			factory.setConfigFile(CODECS_CONFIG_FILE);
			factory.openConfigFile();
			std::string codec_id = factory.addCodec(this->m_config_ptr);
			this->p = factory.getCodec(codec_id);
		} else {
			this->m_original_codec_ptr->setConfig(m_vocab_mgr.getVocabulary(), this->m_config_ptr);
			this->p = (lineage == CREATED? this->m_original_codec_ptr : this->m_original_codec_ptr->clone());
		}
	}
	~ConfiguredCodecPtr_F() {
	}

	const std::string NAME_1;
	const std::string EVENT_TYPE_1;
	const std::string FIELD_TERM_1;
	const std::string FIELD_NAME_1;
	VocabularyManager m_vocab_mgr;
};

// This might eventually become part of Event itself, but first it would need to, at least,
// be updated to accommodate reordered Event entries, and have tests of its own.
bool operator==(const Event& e1, const Event& e2) {
	Event::ConstIterator it_1 = e1.begin();
	Event::ConstIterator it_2 = e2.begin();
	while (it_1 != e1.end() && it_2 != e2.end()) {
		if (it_1->term_ref != it_2->term_ref)
			return false;
		//if (it_1->value != it_2->value)	// boost::variant doesn't define operator!=
		if (!(it_1->value == it_2->value))
			return false;
		++it_1;
		++it_2;
	}
	return (it_1 == e1.end() && it_2 == e2.end());
}

typedef boost::mpl::list<
	ConfiguredCodecPtr_F<LogCodec_name, CREATED>,
	ConfiguredCodecPtr_F<LogCodec_name, CLONED>,
	ConfiguredCodecPtr_F<LogCodec_name, MANUFACTURED>,
	ConfiguredCodecPtr_F<JSONCodec_name, CREATED>,
	ConfiguredCodecPtr_F<JSONCodec_name, CLONED>,
	ConfiguredCodecPtr_F<JSONCodec_name, MANUFACTURED>,
	ConfiguredCodecPtr_F<XMLCodec_name, CREATED>,
	ConfiguredCodecPtr_F<XMLCodec_name, CLONED>,
	ConfiguredCodecPtr_F<XMLCodec_name, MANUFACTURED>
> ConfiguredCodecPtr_fixture_list;

// ConfiguredCodecPtr_S contains tests that should pass for any type of Codec.
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(ConfiguredCodecPtr_S, ConfiguredCodecPtr_fixture_list)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetName) {
	BOOST_CHECK_EQUAL(F::p->getName(), F::NAME_1);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetComment) {
	BOOST_CHECK_EQUAL(F::p->getComment(), "");
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetEventType) {
	Event::EventType expected_event_type_ref = F::m_vocab_mgr.getVocabulary().findTerm(F::EVENT_TYPE_1);
	BOOST_CHECK_EQUAL(F::p->getEventType(), expected_event_type_ref);
}

// This is just one basic test of Codec::clone(), which is primarily being tested via fixtures
// CodecPtr_F<*, CLONED>, ConfiguredCodecPtr_F<*, CLONED>, etc.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkClone) {
	// Note that p might already be a clone, depending on the fixture.
	BOOST_CHECK(F::p->clone());

	// just one simple check of 'cloneness'
	BOOST_CHECK(F::p->clone()->getContentType() == F::p->getContentType());
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadWithEmptyString) {
	EventFactory event_factory;
	EventPtr ep(event_factory.create(F::p->getEventType()));
	std::stringstream ss("");
	BOOST_CHECK(F::p->read(ss, *ep) == false);
}

// It's convenient to have this test in this suite, but note that the input string can't be
// universally valid, so read() could legitimately throw a different exception due to the
// input string, without ever touching the event.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadWithEventOfUndefinedType) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	EventFactory event_factory;
	EventPtr ep(event_factory.create(Vocabulary::UNDEFINED_TERM_REF));
	std::stringstream ss("some text\n");
	BOOST_CHECK_THROW(F::p->read(ss, *ep), Codec::WrongEventTypeException);
}

// See comment for previous test, checkReadWithEventOfUndefinedType.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadWithEventOfWrongType) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	Event::EventType other_type = F::m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#useragent");
	BOOST_REQUIRE(other_type != F::p->getEventType());
	EventFactory event_factory;
	EventPtr ep(event_factory.create(other_type));
	std::stringstream ss("some text\n");
	BOOST_CHECK_THROW(F::p->read(ss, *ep), Codec::WrongEventTypeException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkWriteOutputsSomething) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	EventAllocator ea;
	Event e(F::p->getEventType(), &ea);
	std::ostringstream out;
	BOOST_CHECK_NO_THROW(F::p->write(out, e));
	BOOST_CHECK(!out.str().empty());
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadOutputOfWrite) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	EventFactory event_factory;
	EventPtr event_ptr(event_factory.create(F::p->getEventType()));
	Vocabulary::TermRef bytes_ref = F::m_vocab_mgr.getVocabulary().findTerm(F::FIELD_TERM_1);
	event_ptr->setUInt(bytes_ref, 42);
	std::ostringstream out;
	BOOST_CHECK_NO_THROW(F::p->write(out, *event_ptr));
	std::string output_str = out.str();
	std::istringstream in(output_str);
	EventPtr event_ptr_2(event_factory.create(F::p->getEventType()));
	BOOST_CHECK(F::p->read(in, *event_ptr_2));
	BOOST_CHECK_EQUAL(event_ptr_2->getUInt(bytes_ref), 42);
	BOOST_CHECK(*event_ptr == *event_ptr_2);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadOutputOfWriteAfterFinish) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	EventFactory event_factory;
	EventPtr event_ptr(event_factory.create(F::p->getEventType()));
	Vocabulary::TermRef bytes_ref = F::m_vocab_mgr.getVocabulary().findTerm(FIELD_TERM_1);
	event_ptr->setUInt(bytes_ref, 42);
	std::ostringstream out;
	BOOST_CHECK_NO_THROW(F::p->write(out, *event_ptr));
	BOOST_CHECK_NO_THROW(F::p->finish(out));
	std::string output_str = out.str();
	std::istringstream in(output_str);
	EventPtr event_ptr_2(event_factory.create(F::p->getEventType()));
	BOOST_CHECK(F::p->read(in, *event_ptr_2));
	BOOST_CHECK_EQUAL(event_ptr_2->getUInt(bytes_ref), 42);
	BOOST_CHECK(*event_ptr == *event_ptr_2);

	BOOST_CHECK(!F::p->read(in, *event_ptr_2));
	BOOST_CHECK(event_ptr_2->empty());
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkReadOutputOfWritingEmptyEvent) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	EventFactory event_factory;
	EventPtr event_ptr(event_factory.create(F::p->getEventType()));
	std::ostringstream out;
	BOOST_CHECK_NO_THROW(F::p->write(out, *event_ptr));
	std::string output_str = out.str();
	std::istringstream in(output_str);
	EventPtr event_ptr_2(event_factory.create(F::p->getEventType()));
	BOOST_CHECK(F::p->read(in, *event_ptr_2));
	BOOST_CHECK(event_ptr_2->empty());
	BOOST_CHECK(*event_ptr == *event_ptr_2);
}

BOOST_AUTO_TEST_SUITE_END()

typedef boost::mpl::list<
	ConfiguredCodecPtr_F<LogCodec_name, CREATED>,
	ConfiguredCodecPtr_F<LogCodec_name, CLONED>,
	ConfiguredCodecPtr_F<JSONCodec_name, CREATED>,
	ConfiguredCodecPtr_F<JSONCodec_name, CLONED>,
	ConfiguredCodecPtr_F<XMLCodec_name, CREATED>,
	ConfiguredCodecPtr_F<XMLCodec_name, CLONED>
> ConfiguredCodecPtrNoFactory_fixture_list;

// ConfiguredCodecPtrNoFactory_S contains tests that should pass for any type of Codec.
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(ConfiguredCodecPtrNoFactory_S, ConfiguredCodecPtrNoFactory_fixture_list)

// This test needs to be in the "No Factory" suite, because in the case where the
// Codec is created by a factory, calling m_vocab_mgr.removeTerm() automatically calls
// updateVocabulary() on the Codec.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkUpdateVocabularyWithOneTermRemoved) {
	SKIP_WITH_WARNING_FOR_UNFINISHED_CODECS
	F::m_vocab_mgr.setLocked("urn:vocab:clickstream", false);
	F::m_vocab_mgr.removeTerm("urn:vocab:clickstream", F::FIELD_TERM_1);
	BOOST_CHECK_THROW(F::p->updateVocabulary(F::m_vocab_mgr.getVocabulary()), Codec::TermNoLongerDefinedException);
}

// This test needs to be in the "No Factory" suite, because in the case where the
// Codec is created by a factory, calling m_vocab_mgr.updateTerm() automatically calls
// updateVocabulary() on the Codec.
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkUpdateVocabularyWithOneTermChanged) {
	const Vocabulary& v = F::m_vocab_mgr.getVocabulary();
	Vocabulary::TermRef term_ref = v.findTerm(F::FIELD_TERM_1);
	Vocabulary::Term modified_term = v[term_ref];
	modified_term.term_comment = "A modified comment";
	F::m_vocab_mgr.setLocked("urn:vocab:clickstream", false);
	F::m_vocab_mgr.updateTerm("urn:vocab:clickstream", modified_term);

	BOOST_CHECK_NO_THROW(F::p->updateVocabulary(F::m_vocab_mgr.getVocabulary()));

	// TODO: write some tests that check that updateVocabulary() actually does something.
}

BOOST_AUTO_TEST_SUITE_END()

typedef ConfiguredCodecPtr_F<LogCodec_name, CREATED> ConfiguredLogCodecPtr_F;
BOOST_FIXTURE_TEST_SUITE(ConfiguredLogCodecPtr_S, ConfiguredLogCodecPtr_F)

BOOST_AUTO_TEST_CASE(checkReadOneEvent) {
	EventFactory event_factory;
	EventPtr event_ptr(event_factory.create(p->getEventType()));
	std::istringstream in("500\n"); // EventType has only one field, FIELD_TERM_1 (urn:vocab:clickstream#bytes)
	BOOST_CHECK(p->read(in, *event_ptr));

	Vocabulary::TermRef bytes_ref = m_vocab_mgr.getVocabulary().findTerm(FIELD_TERM_1);
	BOOST_CHECK_EQUAL(event_ptr->getUInt(bytes_ref), 500);
}

BOOST_AUTO_TEST_SUITE_END()

typedef ConfiguredCodecPtr_F<JSONCodec_name, CREATED> ConfiguredJSONCodecPtr_F;
BOOST_FIXTURE_TEST_SUITE(ConfiguredJSONCodecPtr_S, ConfiguredJSONCodecPtr_F)

BOOST_AUTO_TEST_SUITE_END()

typedef ConfiguredCodecPtr_F<JSONCodec_name, CREATED> ConfiguredXMLCodecPtr_F;
BOOST_FIXTURE_TEST_SUITE(ConfiguredXMLCodecPtr_S, ConfiguredXMLCodecPtr_F)

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(codecFactoryCreationAndDestruction_S)

BOOST_AUTO_TEST_CASE(checkCodecFactoryConstructor) {
	VocabularyManager vocab_mgr;
	BOOST_CHECK_NO_THROW(CodecFactory codecFactory(vocab_mgr));
}

BOOST_AUTO_TEST_CASE(checkCodecFactoryDestructor) {
	VocabularyManager vocab_mgr;
	CodecFactory* codecFactory = new CodecFactory(vocab_mgr);
	BOOST_CHECK_NO_THROW(delete codecFactory);
}

BOOST_AUTO_TEST_CASE(checkLockVocabularyManagerAfterCodecFactoryDestroyed) {
	VocabularyManager vocab_mgr;
	vocab_mgr.setConfigFile(get_vocabularies_file());
	vocab_mgr.openConfigFile();
	{
		CodecFactory codecFactory(vocab_mgr);
	}

	// This is a placeholder to alert people that this test is failing, without
	// having to actually call the offending line of code, which crashes the tests.
	BOOST_FAIL("This test would cause a crash if the next line were executed");

	// The problem here is that vocab_mgr is trying to signal the destroyed factory,
	// which had registered with it for updates.
	vocab_mgr.setLocked("urn:vocab:clickstream", false);
}

BOOST_AUTO_TEST_SUITE_END()


class NewCodecFactory_F : public CodecFactory {
public:
	NewCodecFactory_F() : CodecFactory(m_vocab_mgr) {
		setup_logging_for_unit_tests();
		cleanup_codec_config_files(false);
		
		if (! m_config_loaded) {
			setup_plugins_directory();
			// load the CLF vocabulary
			m_vocab_mgr.setConfigFile(get_vocabularies_file());
			m_vocab_mgr.openConfigFile();
			m_config_loaded = true;
		}

		m_codec_id = "some_ID";

		// create a new codec configuration file
		setConfigFile(CODECS_CONFIG_FILE);
		createConfigFile();

		// check new codec config file
		// ...
	}
	~NewCodecFactory_F() {
	}

	/**
	 * returns a valid configuration tree for a Codec
	 *
	 * @param plugin_type the type of new plugin that is being created
	 *
	 * @return xmlNodePtr XML configuration list for the new Codec
	 */
	inline xmlNodePtr createCodecConfig(const std::string& plugin_type) {
		xmlNodePtr config_ptr(ConfigManager::createPluginConfig(plugin_type));
		xmlNodePtr event_type_node = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>("EventType"));
		xmlNodeSetContent(event_type_node,  reinterpret_cast<const xmlChar*>("urn:vocab:clickstream#http-request"));
		xmlAddNextSibling(config_ptr, event_type_node);
		return config_ptr;
	}

	std::string m_codec_id;
	static VocabularyManager m_vocab_mgr;
	static bool m_config_loaded;
};

VocabularyManager	NewCodecFactory_F::m_vocab_mgr;
bool				NewCodecFactory_F::m_config_loaded = false;


BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(NewCodecFactory_S, 
									   boost::mpl::list<NewCodecFactory_F>)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkLoadLogCodec) {
	xmlNodePtr config_ptr(F::createCodecConfig("LogCodec"));
	BOOST_CHECK_NO_THROW(F::addCodec(config_ptr));
	xmlFreeNodeList(config_ptr);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkLoadJSONCodec) {
	xmlNodePtr config_ptr(F::createCodecConfig("JSONCodec"));
	BOOST_CHECK_NO_THROW(F::addCodec(config_ptr));
	xmlFreeNodeList(config_ptr);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkLoadXMLCodec) {
	xmlNodePtr config_ptr(F::createCodecConfig("XMLCodec"));
	BOOST_CHECK_NO_THROW(F::addCodec(config_ptr));
	xmlFreeNodeList(config_ptr);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkLoadMultipleCodecs) {
	xmlNodePtr config_ptr(F::createCodecConfig("LogCodec"));
	BOOST_CHECK_NO_THROW(F::addCodec(config_ptr));
	xmlNodeSetContent(config_ptr,  reinterpret_cast<const xmlChar*>("JSONCodec"));
	BOOST_CHECK_NO_THROW(F::addCodec(config_ptr));
	xmlNodeSetContent(config_ptr,  reinterpret_cast<const xmlChar*>("XMLCodec"));
	BOOST_CHECK_NO_THROW(F::addCodec(config_ptr));
	xmlFreeNodeList(config_ptr);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkLoadUnknownCodec) {
	xmlNodePtr config_ptr(F::createCodecConfig("UnknownCodec"));
	BOOST_CHECK_THROW(F::addCodec(config_ptr), PionPlugin::PluginNotFoundException);
	xmlFreeNodeList(config_ptr);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigForMissingCodec) {
	BOOST_CHECK_THROW(F::setCodecConfig(F::m_codec_id, NULL), CodecFactory::CodecNotFoundException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkRemoveCodec) {
	BOOST_CHECK_THROW(F::removeCodec(F::m_codec_id), CodecFactory::CodecNotFoundException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetCodec) {
	BOOST_CHECK_THROW(F::getCodec(F::m_codec_id), CodecFactory::CodecNotFoundException);
}

BOOST_AUTO_TEST_SUITE_END()


template<const char* plugin_name>
class CodecFactoryWithCodecLoaded_F : public NewCodecFactory_F {
public:
	CodecFactoryWithCodecLoaded_F() {
		m_plugin_name = plugin_name;
		xmlNodePtr config_ptr(NewCodecFactory_F::createCodecConfig(plugin_name));
		m_codec_id = addCodec(config_ptr);
		xmlFreeNodeList(config_ptr);
	}

	std::string m_plugin_name;
};

typedef boost::mpl::list<CodecFactoryWithCodecLoaded_F<LogCodec_name>,
						 CodecFactoryWithCodecLoaded_F<JSONCodec_name>,
						 CodecFactoryWithCodecLoaded_F<XMLCodec_name> > codec_fixture_list_2;

// CodecFactoryWithCodecLoaded_S contains tests that should pass for any type of Codec
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithCodecLoaded_S, codec_fixture_list_2)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetCodec) {
	BOOST_CHECK(F::getCodec(F::m_codec_id));
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkRemoveCodec) {
	BOOST_CHECK_NO_THROW(F::removeCodec(F::m_codec_id));
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigMissingEventType) {
	BOOST_CHECK_THROW(F::setCodecConfig(F::m_codec_id, NULL), Codec::EmptyEventException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigUnknownEventType) {
	xmlNodePtr event_type_node = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>("EventType"));
	xmlNodeSetContent(event_type_node,  reinterpret_cast<const xmlChar*>("NotAType"));

	BOOST_CHECK_THROW(F::setCodecConfig(F::m_codec_id, event_type_node), Codec::UnknownTermException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigEventTypeNotAnObject) {
	xmlNodePtr event_type_node = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>("EventType"));
	xmlNodeSetContent(event_type_node,  reinterpret_cast<const xmlChar*>("urn:vocab:clickstream#remotehost"));

	BOOST_CHECK_THROW(F::setCodecConfig(F::m_codec_id, event_type_node), Codec::NotAnObjectException);
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetNewCodecConfiguration) {
	xmlNodePtr comment_node = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>("Comment"));
	xmlNodeSetContent(comment_node,  reinterpret_cast<const xmlChar*>("A new comment"));
	xmlNodePtr event_type_node = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>("EventType"));
	xmlNodeSetContent(event_type_node,  reinterpret_cast<const xmlChar*>("urn:vocab:clickstream#http-request"));
	xmlAddNextSibling(comment_node, event_type_node);

	BOOST_CHECK_NO_THROW(F::setCodecConfig(F::m_codec_id, comment_node));
	xmlFreeNodeList(comment_node);

	// check codec config file
	// ...
}

BOOST_AUTO_TEST_SUITE_END()


// CodecFactoryWithLogCodecLoaded_S contains tests that are specific to LogCodecs
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithLogCodecLoaded_S, boost::mpl::list<CodecFactoryWithCodecLoaded_F<LogCodec_name> >)

//BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigX) {
//	...
//	BOOST_CHECK_NO_THROW(F::setCodecConfig(F::m_codec_id, log_codec_options));
//	...
//}

BOOST_AUTO_TEST_SUITE_END()


// CodecFactoryWithJSONCodecLoaded_S contains tests that are specific to JSONCodecs
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithJSONCodecLoaded_S, boost::mpl::list<CodecFactoryWithCodecLoaded_F<JSONCodec_name> >)

//BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigX) {
//	...
//	BOOST_CHECK_NO_THROW(F::setCodecConfig(F::m_codec_id, json_codec_options));
//	...
//}

BOOST_AUTO_TEST_SUITE_END()


// CodecFactoryWithXMLCodecLoaded_S contains tests that are specific to XMLCodecs
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithXMLCodecLoaded_S, boost::mpl::list<CodecFactoryWithCodecLoaded_F<XMLCodec_name> >)

//BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkSetCodecConfigX) {
//	...
//	BOOST_CHECK_NO_THROW(F::setCodecConfig(F::m_codec_id, xml_codec_options));
//	...
//}

BOOST_AUTO_TEST_SUITE_END()


class CodecFactoryWithMultipleCodecsLoaded_F : public NewCodecFactory_F {
public:
	CodecFactoryWithMultipleCodecsLoaded_F() {
		xmlNodePtr config_ptr(NewCodecFactory_F::createCodecConfig(LogCodec_name));
		m_LogCodec_id = addCodec(config_ptr);
		xmlNodeSetContent(config_ptr,  reinterpret_cast<const xmlChar*>(JSONCodec_name));
		m_JSONCodec_id = addCodec(config_ptr);
		xmlNodeSetContent(config_ptr,  reinterpret_cast<const xmlChar*>(XMLCodec_name));
		m_XMLCodec_id = addCodec(config_ptr);
		xmlFreeNodeList(config_ptr);
	}

	std::string m_LogCodec_id;
	std::string m_JSONCodec_id;
	std::string m_XMLCodec_id;
};

BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithMultipleCodecsLoaded_S, 
									   boost::mpl::list<CodecFactoryWithMultipleCodecsLoaded_F>)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetCodec) {
	BOOST_CHECK(F::getCodec(F::m_LogCodec_id));
	BOOST_CHECK(F::getCodec(F::m_JSONCodec_id));
	BOOST_CHECK(F::getCodec(F::m_XMLCodec_id));
}

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkRemoveCodec) {
	BOOST_CHECK_NO_THROW(F::removeCodec(F::m_LogCodec_id));
	BOOST_CHECK_NO_THROW(F::removeCodec(F::m_JSONCodec_id));
	BOOST_CHECK_NO_THROW(F::removeCodec(F::m_XMLCodec_id));
}

// TODO: check that all the codecs got their vocabulary updated
BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkUpdateVocabulary) {
	BOOST_CHECK_NO_THROW(F::updateVocabulary());
}

BOOST_AUTO_TEST_SUITE_END()


template<const char* plugin_name>
class CodecFactoryWithCodecPtr_F : public CodecFactoryWithCodecLoaded_F<plugin_name> {
public:
	CodecFactoryWithCodecPtr_F() {
		BOOST_REQUIRE(m_codec_ptr = this->getCodec(this->m_codec_id));
	}

	CodecPtr m_codec_ptr;
};

typedef boost::mpl::list<CodecFactoryWithCodecPtr_F<LogCodec_name>,
						 CodecFactoryWithCodecPtr_F<JSONCodec_name>,
						 CodecFactoryWithCodecPtr_F<XMLCodec_name> > codec_fixture_list_3;

// CodecFactoryWithCodecPtr_S contains tests that should pass for any type of Codec.
// It's empty now because the tests that were in it are now in ConfiguredCodecPtr_S,
// but I'll leave it for now since the fixture's still being used, and it might be
// a good place for tests that are specific to factory generated Codecs and need
// access to the factory.
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithCodecPtr_S, codec_fixture_list_3)

BOOST_AUTO_TEST_SUITE_END()


// CodecFactoryWithLogCodecPtr_S contains tests that are specific to LogCodecs
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithLogCodecPtr_S, boost::mpl::list<CodecFactoryWithCodecPtr_F<LogCodec_name> >)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetContentType) {
	BOOST_CHECK_EQUAL(F::m_codec_ptr->getContentType(), "text/ascii");
}

BOOST_AUTO_TEST_SUITE_END()


// CodecFactoryWithJSONCodecPtr_S contains tests that are specific to JSONCodecs
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithJSONCodecPtr_S, boost::mpl::list<CodecFactoryWithCodecPtr_F<JSONCodec_name> >)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetContentType) {
	BOOST_CHECK_EQUAL(F::m_codec_ptr->getContentType(), "text/json");
}

BOOST_AUTO_TEST_SUITE_END()


// CodecFactoryWithXMLCodecPtr_S contains tests that are specific to XMLCodecs
BOOST_AUTO_TEST_SUITE_FIXTURE_TEMPLATE(CodecFactoryWithXMLCodecPtr_S, boost::mpl::list<CodecFactoryWithCodecPtr_F<XMLCodec_name> >)

BOOST_AUTO_TEST_CASE_FIXTURE_TEMPLATE(checkGetContentType) {
	BOOST_CHECK_EQUAL(F::m_codec_ptr->getContentType(), "text/xml");
}

BOOST_AUTO_TEST_SUITE_END()


class CodecFactoryLogFormatTests_F : public CodecFactory {
public:
	CodecFactoryLogFormatTests_F()
		: CodecFactory(m_vocab_mgr),
		m_common_id("a174c3b0-bfcd-11dc-9db2-0016cb926e68"),
		m_combined_id("3f49f2da-bfe3-11dc-8875-0016cb926e68"),
		m_extended_id("23f68d5a-bfec-11dc-81a7-0016cb926e68"),
		m_justdate_id("dba9eac2-d8bb-11dc-bebe-001cc02bd66b")
	{
		setup_logging_for_unit_tests();
		cleanup_codec_config_files(false);
		boost::filesystem::copy_file(CODECS_TEMPLATE_FILE, CODECS_CONFIG_FILE);

		if (! m_config_loaded) {
			setup_plugins_directory();
			// load the CLF vocabulary
			m_vocab_mgr.setConfigFile(get_vocabularies_file());
			m_vocab_mgr.openConfigFile();
			m_config_loaded = true;
		}

		setConfigFile(CODECS_CONFIG_FILE);
		openConfigFile();

		m_common_codec = getCodec(m_common_id);
		BOOST_CHECK(m_common_codec);
		m_combined_codec = getCodec(m_combined_id);
		BOOST_CHECK(m_combined_codec);
		m_extended_codec = getCodec(m_extended_id);
		BOOST_CHECK(m_extended_codec);
		m_date_codec = getCodec(m_justdate_id);
		BOOST_CHECK(m_date_codec);

		m_remotehost_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#remotehost");
		m_rfc931_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#rfc931");
		m_authuser_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#authuser");
		m_date_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#date");
		m_request_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#request");
		m_status_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#status");
		m_bytes_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#bytes");
		m_referer_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#referer");
		m_useragent_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#useragent");
	}
	~CodecFactoryLogFormatTests_F() {}

	EventFactory		m_event_factory;
	const std::string	m_common_id;
	const std::string	m_combined_id;
	const std::string	m_extended_id;
	const std::string	m_justdate_id;
	CodecPtr			m_common_codec;
	CodecPtr			m_combined_codec;
	CodecPtr			m_extended_codec;
	CodecPtr			m_date_codec;
	Vocabulary::TermRef	m_remotehost_ref;
	Vocabulary::TermRef	m_rfc931_ref;
	Vocabulary::TermRef	m_authuser_ref;
	Vocabulary::TermRef	m_date_ref;
	Vocabulary::TermRef	m_request_ref;
	Vocabulary::TermRef	m_status_ref;
	Vocabulary::TermRef	m_bytes_ref;
	Vocabulary::TermRef	m_referer_ref;
	Vocabulary::TermRef	m_useragent_ref;

	static VocabularyManager m_vocab_mgr;
	static bool	m_config_loaded;
};

VocabularyManager	CodecFactoryLogFormatTests_F::m_vocab_mgr;
bool				CodecFactoryLogFormatTests_F::m_config_loaded = false;

// CodecFactoryWithCodecLoaded_S contains tests for the common log format
BOOST_FIXTURE_TEST_SUITE(CodecFactoryLogFormatTests_S, CodecFactoryLogFormatTests_F)

BOOST_AUTO_TEST_CASE(checkGetCodec) {
	BOOST_CHECK(getCodec(m_common_id));
	BOOST_CHECK(getCodec(m_combined_id));
	BOOST_CHECK(getCodec(m_extended_id));
}

BOOST_AUTO_TEST_CASE(checkCommonCodecEventTypes) {
	const Event::EventType event_type_ref = m_vocab_mgr.getVocabulary().findTerm("urn:vocab:clickstream#http-request");
	BOOST_CHECK_EQUAL(m_common_codec->getEventType(), event_type_ref);
	BOOST_CHECK_EQUAL(m_combined_codec->getEventType(), event_type_ref);
	BOOST_CHECK_EQUAL(m_extended_codec->getEventType(), event_type_ref);
}

BOOST_AUTO_TEST_CASE(checkCommonCodecName) {
	BOOST_CHECK_EQUAL(m_common_codec->getName(), "Common Log Format");
	BOOST_CHECK_EQUAL(m_combined_codec->getName(), "Combined Log Format");
	BOOST_CHECK_EQUAL(m_extended_codec->getName(), "Extended Log Format");
}

BOOST_AUTO_TEST_CASE(checkCommonCodecComment) {
	BOOST_CHECK_EQUAL(m_common_codec->getComment(), "Codec for the Common Log Format (CLF)");
	BOOST_CHECK_EQUAL(m_combined_codec->getComment(), "Codec for the Combined Log Format (DLF)");
	BOOST_CHECK_EQUAL(m_extended_codec->getComment(), "Codec for the Extended Log Format (ELF)");
}

BOOST_AUTO_TEST_CASE(checkJustDateCodecReadEntry) {
	BOOST_REQUIRE(m_date_codec);
	std::stringstream ss("\"05/Apr/2007:05:37:11 -0600\"\n");
	EventPtr event_ptr(m_event_factory.create(m_date_codec->getEventType()));
	BOOST_REQUIRE(m_date_codec->read(ss, *event_ptr));
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref).date(),
					  boost::gregorian::date(2007, 4, 5));
}

BOOST_AUTO_TEST_CASE(checkCommonCodecReadLogFile) {
	// open the CLF log file
	std::ifstream in;
	in.open(COMMON_LOG_FILE.c_str(), std::ios::in);
	BOOST_REQUIRE(in.is_open());

	// read the first record
	EventPtr event_ptr(m_event_factory.create(m_common_codec->getEventType()));
	BOOST_REQUIRE(m_common_codec->read(in, *event_ptr));
	// check the first data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.19.111");
	BOOST_CHECK(! event_ptr->isDefined(m_rfc931_ref));
	BOOST_CHECK(! event_ptr->isDefined(m_authuser_ref));
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2007, 4, 5),
								   boost::posix_time::time_duration(5, 37, 11)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /robots.txt HTTP/1.0");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 404UL);
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_bytes_ref), 208UL);

	// read the second record
	event_ptr->clear();
	BOOST_REQUIRE(m_common_codec->read(in, *event_ptr));
	// check the second data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.31.104");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_rfc931_ref), "ab");
	BOOST_CHECK(! event_ptr->isDefined(m_authuser_ref));
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2007, 6, 8),
								   boost::posix_time::time_duration(7, 20, 2)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /community/ HTTP/1.1");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 200UL);
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_bytes_ref), 3546UL);

	// read the third record
	event_ptr->clear();
	BOOST_REQUIRE(m_common_codec->read(in, *event_ptr));
	// check the third data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.2.104");
	BOOST_CHECK(! event_ptr->isDefined(m_rfc931_ref));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_authuser_ref), "cd");
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2007, 9, 24),
								   boost::posix_time::time_duration(12, 13, 3)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /default.css HTTP/1.1");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 200UL);
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_bytes_ref), 6698UL);

	// read the forth record
	event_ptr->clear();
	BOOST_REQUIRE(m_common_codec->read(in, *event_ptr));
	// check the forth data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.141.122");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_rfc931_ref), "ef");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_authuser_ref), "gh");
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2008, 1, 30),
								   boost::posix_time::time_duration(15, 26, 7)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /pion/ HTTP/1.1");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 200UL);
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_bytes_ref), 7058UL);
}

BOOST_AUTO_TEST_CASE(checkCommonCodecWriteLogFormatJustOneField) {
	EventPtr event_ptr(m_event_factory.create(m_common_codec->getEventType()));
	event_ptr->setString(m_remotehost_ref, "192.168.0.1");
	std::stringstream ss;
	m_common_codec->write(ss, *event_ptr);
	BOOST_CHECK_EQUAL(ss.str(), "192.168.0.1 - - [] \"\" - -\x0A");
}

BOOST_AUTO_TEST_CASE(checkCommonCodecWriteLogFormatAllFields) {
	EventPtr event_ptr(m_event_factory.create(m_common_codec->getEventType()));
	event_ptr->setString(m_remotehost_ref, "192.168.10.10");
	event_ptr->setString(m_rfc931_ref, "greg");
	event_ptr->setString(m_authuser_ref, "bob");
	event_ptr->setDateTime(m_date_ref, PionDateTime(boost::gregorian::date(2008, 1, 10),
													boost::posix_time::time_duration(12, 31, 0)));
	event_ptr->setString(m_request_ref, "GET / HTTP/1.1");
	event_ptr->setUInt(m_status_ref, 302);
	event_ptr->setUInt(m_bytes_ref, 116);
	std::stringstream ss;
	m_common_codec->write(ss, *event_ptr);
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(ss.str(), "192.168.10.10 greg bob [10/Jan/2008:12:31:00 ] \"GET / HTTP/1.1\" 302 116\x0A");
}

BOOST_AUTO_TEST_CASE(checkCombinedCodecReadLogFile) {
	// open the CLF log file
	std::ifstream in;
	in.open(COMBINED_LOG_FILE.c_str(), std::ios::in);
	BOOST_REQUIRE(in.is_open());

	// read the first record
	EventPtr event_ptr(m_event_factory.create(m_combined_codec->getEventType()));
	BOOST_REQUIRE(m_combined_codec->read(in, *event_ptr));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_referer_ref), "http://www.example.com/start.html");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_useragent_ref), "Mozilla/4.08 [en] (Win98; I ;Nav)");

	// read the second record
	event_ptr->clear();
	BOOST_REQUIRE(m_combined_codec->read(in, *event_ptr));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_referer_ref), "http://www.atomiclabs.com/");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_useragent_ref), "Mozilla/4.08 [en] (Win98; I ;Nav)");

	// read the third record
	event_ptr->clear();
	BOOST_REQUIRE(m_combined_codec->read(in, *event_ptr));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_referer_ref), "http://www.google.com/");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_useragent_ref), "Mozilla/5.0 (Macintosh; U; PPC Mac OS X Mach-O; en-US; rv:1.7a) Gecko/20040614 Firefox/0.9.0+");

	// read the forth record
	event_ptr->clear();
	BOOST_REQUIRE(m_combined_codec->read(in, *event_ptr));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_referer_ref), "http://www.wikipedia.com/");
	BOOST_CHECK_EQUAL(event_ptr->getString(m_useragent_ref), "Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)");
}

BOOST_AUTO_TEST_CASE(checkCombinedCodecWriteJustExtraFields) {
	EventPtr event_ptr(m_event_factory.create(m_combined_codec->getEventType()));
	event_ptr->setString(m_referer_ref, "http://www.atomiclabs.com/");
	event_ptr->setString(m_useragent_ref, "Mozilla/4.08 [en] (Win98; I ;Nav)");
	std::stringstream ss;
	m_combined_codec->write(ss, *event_ptr);
	BOOST_CHECK_EQUAL(ss.str(), "- - - [] \"\" - - \"http://www.atomiclabs.com/\" \"Mozilla/4.08 [en] (Win98; I ;Nav)\"\x0A");
}

BOOST_AUTO_TEST_CASE(checkExtendedCodecReadLogFile) {
	// open the CLF log file
	std::ifstream in;
	in.open(EXTENDED_LOG_FILE.c_str(), std::ios::in);
	BOOST_REQUIRE(in.is_open());

	// read the first record
	EventPtr event_ptr(m_event_factory.create(m_extended_codec->getEventType()));
	BOOST_REQUIRE(m_extended_codec->read(in, *event_ptr));
	// check the third data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.2.104");
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2007, 9, 24),
								   boost::posix_time::time_duration(12, 13, 3)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /default.css HTTP/1.1");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 200UL);

	// read the second record
	event_ptr->clear();
	BOOST_REQUIRE(m_extended_codec->read(in, *event_ptr));
	// check the forth data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.141.122");
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2008, 1, 30),
								   boost::posix_time::time_duration(15, 26, 7)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /pion/ HTTP/1.1");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 200UL);

	// read the third record
	event_ptr->clear();
	BOOST_REQUIRE(m_extended_codec->read(in, *event_ptr));
	// check the first data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.19.111");
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2007, 4, 5),
								   boost::posix_time::time_duration(5, 37, 11)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /robots.txt HTTP/1.0");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 404UL);

	// read the forth record
	event_ptr->clear();
	BOOST_REQUIRE(m_extended_codec->read(in, *event_ptr));
	// check the second data
	BOOST_CHECK_EQUAL(event_ptr->getString(m_remotehost_ref), "10.0.31.104");
	// NOTE: timezone offsets are currently not working in DateTimeFacet
	BOOST_CHECK_EQUAL(event_ptr->getDateTime(m_date_ref),
					  PionDateTime(boost::gregorian::date(2007, 6, 8),
								   boost::posix_time::time_duration(7, 20, 2)));
	BOOST_CHECK_EQUAL(event_ptr->getString(m_request_ref), "GET /community/ HTTP/1.1");
	BOOST_CHECK_EQUAL(event_ptr->getUInt(m_status_ref), 200UL);
}

BOOST_AUTO_TEST_CASE(checkExtendedCodecWrite) {
	EventPtr event_ptr(m_event_factory.create(m_extended_codec->getEventType()));
	event_ptr->setString(m_remotehost_ref, "192.168.10.10");
	event_ptr->setDateTime(m_date_ref, PionDateTime(boost::gregorian::date(2008, 1, 10),
										   boost::posix_time::time_duration(12, 31, 0)));
	event_ptr->setString(m_request_ref, "GET / HTTP/1.1");
	event_ptr->setString(m_referer_ref, "http://www.atomiclabs.com/");
	event_ptr->setUInt(m_status_ref, 302);
	std::stringstream ss;
	m_extended_codec->write(ss, *event_ptr);
	m_extended_codec->write(ss, *event_ptr);
	BOOST_CHECK_EQUAL(ss.str(), "#Version: 1.0\x0A#Fields: date remotehost request cs(Referer) status\x0A\"10/Jan/2008:12:31:00 \" 192.168.10.10 \"GET / HTTP/1.1\" \"http://www.atomiclabs.com/\" 302\x0A\"10/Jan/2008:12:31:00 \" 192.168.10.10 \"GET / HTTP/1.1\" \"http://www.atomiclabs.com/\" 302\x0A");
}

BOOST_AUTO_TEST_SUITE_END()
