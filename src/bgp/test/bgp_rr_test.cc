#include <boost/scoped_ptr.hpp>
#include <pugixml/pugixml.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_message_test.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace std;

class BgpAttributeTest : public ::testing::Test {
  protected:
    BgpAttributeTest() : server_(&evm_), attr_db_(server_.attr_db()) {
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
    }

    static BgpAttribute *BgpAttributeFind(BgpAttrSpec *spec,
                                          BgpAttribute::Code code);
    static const BgpAttribute *BgpAttributeFind(const BgpAttrSpec &spec,
                                         BgpAttribute::Code code);
    static std::string BgpAttributeToString(const BgpAttrSpec &spec);

    EventManager evm_;
    BgpServer server_;
    BgpAttrDB *attr_db_;
};

BgpAttribute *BgpAttributeTest::BgpAttributeFind(BgpAttrSpec *spec,
                                                 BgpAttribute::Code code) {
    for (vector<BgpAttribute *>::iterator iter = spec->begin();
         iter != spec->end(); ++iter) {
        if ((*iter)->code == code) {
            return *iter;
        }
    }
    return NULL;
}

const BgpAttribute *BgpAttributeTest::BgpAttributeFind(
    const BgpAttrSpec &spec, BgpAttribute::Code code) {
    for (vector<BgpAttribute *>::const_iterator iter = spec.begin();
         iter != spec.end(); ++iter) {
        if ((*iter)->code == code) {
            return *iter;
        }
    }
    return NULL;
}

std::string BgpAttributeTest::BgpAttributeToString(const BgpAttrSpec &spec) {
    std::stringstream ss;
    for (vector<BgpAttribute *>::const_iterator iter = spec.begin();
         iter != spec.end(); ++iter) {
        ss << (*iter)->ToString();
        ss << std::endl;
    }
    return ss.str();
}

static std::string HexString(const uint8_t *data, size_t size) {
    std::stringstream ss;
    for (int i = 0; i < size; ++i) {
        if (i) {
            ss << " ";
        }
        ss << std::hex << (int) data[i];
    }
    return ss.str();
}

/*
 * Decode, parse and encode the originator-id attribute
 */
TEST_F(BgpAttributeTest, OriginatorId) {
    const u_int8_t data[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x30,     // Length
        0x02,           // Update
        0x00, 0x00,     // Withdrawn Routes Length
        0x00, 0x19,     // Path Attribute Length
        0x40, 0x01, 0x01, 0x01, // ORIGIN
        0x40, 0x02, 0x04, 0x02, 0x01, 0x00, 0x01, // AS_PATH
        0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64, // LOCAL_PREF
        0x80, 0x09, 0x04, 0x0a, 0x0b, 0x0c, 0x0d, // ORIGINATOR
    };
    boost::scoped_ptr<const BgpProto::Update> update(
        static_cast<const BgpProto::Update *>(
            BgpProto::Decode(data, sizeof(data))));
    ASSERT_TRUE(update.get() != NULL);

    const BgpAttrOriginatorId *oid =
            static_cast<const BgpAttrOriginatorId *>(
                BgpAttributeFind(update->path_attributes,
                                 BgpAttribute::OriginatorId));
    ASSERT_TRUE(oid != NULL);
    EXPECT_EQ(0x0a0b0c0dul, oid->originator_id);

    uint8_t buffer[4096];
    int res = BgpProto::Encode(update.get(), buffer, sizeof(buffer));
    EXPECT_EQ(sizeof(data), res);
    EXPECT_EQ(0, memcmp(buffer, data, sizeof(data)));

    LOG(DEBUG, BgpAttributeToString(update->path_attributes));

    // Locate the attribute in the database
    BgpAttrPtr attr = attr_db_->Locate(update->path_attributes);
    EXPECT_EQ(0x0a0b0c0dul, attr->originator_id());
}

TEST_F(BgpAttributeTest, OriginIdComparatorTest) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::IPv4, BgpAf::Vpn);

    BgpAttrSpec nspec = update.path_attributes;
    BgpAttrOriginatorId oid;
    oid.originator_id = 0x0a0b0c0d;
    nspec.push_back(&oid);

    BgpAttrPtr attr1 = attr_db_->Locate(nspec);
    EXPECT_EQ(0x0a0b0c0dul, attr1->originator_id());

    BgpAttrOriginatorId *attr_id =
            static_cast<BgpAttrOriginatorId *>(
                BgpAttributeFind(&nspec, BgpAttribute::OriginatorId));
    ASSERT_TRUE(attr_id != NULL);

    attr_id->originator_id = 0x0a0b0f0ful;
    BgpAttrPtr attr2 = attr_db_->Locate(nspec);
    EXPECT_EQ(0x0a0b0f0ful, attr2->originator_id());
    EXPECT_FALSE(attr1.get() == attr2.get());

    attr_id->originator_id = 0x0a0b0f0dul;
    BgpAttrPtr attr3 = attr_db_->Locate(nspec);
    EXPECT_EQ(0x0a0b0f0dul, attr3->originator_id());
    EXPECT_FALSE(attr1.get() == attr3.get());

    attr_id->originator_id = 0x0a0b0c0dul;
    BgpAttrPtr attr4 = attr_db_->Locate(nspec);
    EXPECT_TRUE(attr1.get() == attr4.get());
}

/*
 * Decode, parse and encode the cluster-list attribute
 */
TEST_F(BgpAttributeTest, ClusterList) {
    const u_int8_t data[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x34,     // Length
        0x02,           // Update
        0x00, 0x00,     // Withdrawn Routes Length
        0x00, 0x1d,     // Path Attribute Length
        0x40, 0x01, 0x01, 0x01, // ORIGIN
        0x40, 0x02, 0x04, 0x02, 0x01, 0x00, 0x01, // AS_PATH
        0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64, // LOCAL_PREF
        // CLUSTER_LIST
        0x80, 0x0a, 0x08, 0x0a, 0x0b, 0x0c, 0x0d, 0xca, 0xfe, 0xd0, 0xd0,
    };
    ParseErrorContext context;
    boost::scoped_ptr<const BgpProto::Update> update(
        static_cast<const BgpProto::Update *>(
            BgpProto::Decode(data, sizeof(data), &context)));
    if (update.get() == NULL) {
        LOG(ERROR, "Parse error: " << context.error_code
            << ", " << context.error_subcode << " " << context.type_name);
        LOG(ERROR, HexString(context.data, min(context.data_size, 16)));
    }
    ASSERT_TRUE(update.get() != NULL);

    const BgpAttrClusterList *clist =
            static_cast<const BgpAttrClusterList *>(
                BgpAttributeFind(update->path_attributes,
                                 BgpAttribute::ClusterList));
    ASSERT_TRUE(clist != NULL);
    EXPECT_EQ(0x0a0b0c0dul, clist->cluster_list[0]);
    EXPECT_EQ(0xcafed0d0ul, clist->cluster_list[1]);

    uint8_t buffer[4096];
    int res = BgpProto::Encode(update.get(), buffer, sizeof(buffer));
    EXPECT_EQ(sizeof(data), res);
    EXPECT_EQ(0, memcmp(buffer, data, sizeof(data)));

    // Locate the attribute in the database
    BgpAttrPtr attr = attr_db_->Locate(update->path_attributes);
    EXPECT_EQ(0x0a0b0c0dul, attr->cluster_list()[0]);
    EXPECT_EQ(0xcafed0d0ul, attr->cluster_list()[1]);
}

TEST_F(BgpAttributeTest, ClusterListComparatorTest) {
    BgpProto::Update update;
    BgpMessageTest::GenerateUpdateMessage(&update, BgpAf::IPv4, BgpAf::Vpn);

    BgpAttrClusterList clist;
    clist.cluster_list.push_back(64512);

    BgpAttrSpec nspec = update.path_attributes;
    nspec.push_back(&clist);

    BgpAttrPtr attr1 = attr_db_->Locate(nspec);
    EXPECT_EQ(64512, attr1->cluster_list()[0]);

    clist.cluster_list.push_back(10458);
    BgpAttrPtr attr2 = attr_db_->Locate(nspec);
    EXPECT_EQ(10458, attr2->cluster_list()[1]);
    EXPECT_FALSE(attr1.get() == attr2.get());

    LOG(DEBUG, clist.ToString());

    clist.cluster_list.pop_back();
    clist.cluster_list.push_back(64513);
    BgpAttrPtr attr3 = attr_db_->Locate(nspec);
    EXPECT_EQ(64513, attr3->cluster_list()[1]);
    EXPECT_FALSE(attr1.get() == attr3.get());

    clist.cluster_list.pop_back();
    BgpAttrPtr attr4 = attr_db_->Locate(nspec);
    EXPECT_TRUE(attr1.get() == attr4.get());
}

class BgpNeighborConfigTest {
  public:
    static void set_cluster_id(BgpNeighborConfig *config, uint32_t cluster_id) {
        config->cluster_id_ = cluster_id;
    }
};

/*
 * Route Reflector test rig
 *
 *
 */
class BgpReflectorTest : public ::testing::Test {
  protected:
    virtual ~BgpReflectorTest() {
        STLDeleteValues(&peers_);
    }

    virtual void SetUp() {
        rr_server_.reset(new BgpServerTest(&evm_, "RR"));
        thread_.reset(new ServerThread(&evm_));

        rr_server_->session_manager()->Initialize(0);

        pugi::xml_node config = xconfig_.append_child("config");
        router_ = config.append_child("bgp-router");
        router_.append_attribute("name") = "RR";
        router_.append_child("autonomous-system").text().set(64512);
        router_.append_child("identifier").text().set("0.0.0.1");
        router_.append_child("address").text().set("127.0.0.1");
        router_.append_child("port").text().set(
            rr_server_->session_manager()->GetPort());
        router_.append_child("address-families").text().set("inet");
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        for (std::vector<BgpServerTest *>::iterator iter = peers_.begin();
             iter != peers_.end(); ++iter) {
            (*iter)->Shutdown();
        }
        task_util::WaitForIdle();
        rr_server_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    pugi::xml_node PeerCreate(const char *name, const char *identifier) {
        BgpServerTest *peer = new BgpServerTest(&evm_, name);
        peers_.push_back(peer);
        peer->session_manager()->Initialize(0);

        LOG(DEBUG, "Peer " << name
            << " port: " << peer->session_manager()->GetPort());

        PeerConfigure(peer, name, identifier,
                      peer->session_manager()->GetPort(),
                      rr_server_->session_manager()->GetPort());

        return ServerPeerConfigure(name, identifier,
                                   peer->session_manager()->GetPort());
    }

    /*
     * Generate the XML configuration for the RR peer (client or mesh peer).
     */
    void PeerConfigure(BgpServerTest *peer, const char *name,
                       const char *identifier,
                       int local_port, int rr_port) {
        pugi::xml_document xdoc;

        pugi::xml_node config = xdoc.append_child("config");
        pugi::xml_node router = config.append_child("bgp-router");
        router.append_attribute("name") = name;
        router.append_child("autonomous-system").text().set(64512);
        router.append_child("identifier").text().set(identifier);
        router.append_child("address").text().set("127.0.0.1");
        router.append_child("port").text().set(local_port);
        router.append_child("address-families").text().set("inet");

        pugi::xml_node rr = config.append_child("bgp-router");
        rr.append_attribute("name") = "RR";
        rr.append_child("autonomous-system").text().set(64512);
        rr.append_child("identifier").text().set("0.0.0.1");
        rr.append_child("address").text().set("127.0.0.1");
        rr.append_child("port").text().set(rr_port);
        rr.append_child("address-families").text().set("inet");

        pugi::xml_node session = router.append_child("session");
        session.append_attribute("to") = "RR";
        session.append_child("address-families").text().set("inet");

        std::ostringstream oss;
        xdoc.save(oss);
        peer->Configure(oss.str());
    }

    pugi::xml_node ServerPeerConfigure(const char *name,
                                       const char *identifier,
                                       int remote_port) {
        pugi::xml_node config = xconfig_.child("config");

        pugi::xml_node peer = config.append_child("bgp-router");
        peer.append_attribute("name") = name;
        peer.append_child("autonomous-system").text().set(64512);
        peer.append_child("identifier").text().set(identifier);
        peer.append_child("address").text().set("127.0.0.1");
        peer.append_child("port").text().set(remote_port);
        peer.append_child("address-families").text().set("inet");

        pugi::xml_node session = router_.append_child("session");
        session.append_attribute("to") = name;
        session.append_child("address-families").text().set("inet");
        return peer;
    }

    void ConfigureBase() {
        PeerCreate("meshpeer", "0.0.0.2");
        pugi::xml_node peer1 = PeerCreate("client1", "0.0.0.3");
        // TODO(roque): cluster-id element is ignored by parser
        peer1.append_child("cluster-id").text().set("0.0.0.1");
        pugi::xml_node peer2 = PeerCreate("client2", "0.0.0.4");
        // TODO(roque): cluster-id element is ignored by parser
        peer2.append_child("cluster-id").text().set("0.0.0.1");
    }

    void ConfiureAddMeshPeer() {
        PeerCreate("meshpeer2", "0.0.0.5");
    }

    void ConfigureAddCluster() {
        pugi::xml_node peer = PeerCreate("client1a", "0.0.0.6");
        // TODO(roque): cluster-id element is ignored by parser
        peer.append_child("cluster-id").text().set("0.0.0.2");
    }

    void ApplyConfig() {
        std::ostringstream oss;
        xconfig_.save(oss);
        rr_server_->Configure(oss.str());
        task_util::WaitForIdle();
    }

    void SetClusterId(const char *name, const char *cluster_id) {
        string uuid = BgpConfigParser::session_uuid("RR", name, 1);
        LOG(DEBUG, "set cluster-id " << name << " id: " << uuid);
        BgpPeer *peer = rr_server_->FindPeerByUuid(
            BgpConfigManager::kMasterInstance, uuid);
        ASSERT_TRUE(peer != NULL);

        BgpNeighborConfig *config = const_cast<BgpNeighborConfig *>(
            peer->config());
        boost::system::error_code err;
        Ip4Address addr = Ip4Address::from_string(cluster_id, err);
        BgpNeighborConfigTest::set_cluster_id(config, addr.to_ulong());
        peer->ConfigUpdate(config);
    }

    void WaitForEstablished() {
        for (std::vector<BgpServerTest *>::const_iterator iter = peers_.begin();
             iter != peers_.end(); ++iter) {
            string uuid = BgpConfigParser::session_uuid(
                "RR", (*iter)->localname(), 1);
            BgpPeer *peer = rr_server_->FindPeerByUuid(
                BgpConfigManager::kMasterInstance, uuid);
            ASSERT_TRUE(peer != NULL);
            BGP_WAIT_FOR_PEER_STATE(peer, StateMachine::ESTABLISHED);
        }
    }

    BgpServerTest *ServerFind(const char *name) {
        for (std::vector<BgpServerTest *>::iterator iter = peers_.begin();
             iter != peers_.end(); ++iter) {
            if ((*iter)->localname() == name) {
                return *iter;
            }
        }
        return NULL;
    }

    void GenerateRoute(BgpServerTest *server, const char *net, int prefixlen) {
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        boost::system::error_code err;
        Ip4Address addr = Ip4Address::from_string(net, err);
        Ip4Prefix prefix(addr, prefixlen);
        request.key.reset(new InetTable::RequestKey(prefix, NULL));
        BgpAttrSpec spec;
        BgpAttrNextHop nexthop(0x7f000001ul);
        spec.push_back(&nexthop);
        BgpAttrPtr attr = server->attr_db()->Locate(spec);
        request.data.reset(new InetTable::RequestData(attr, 0, 0));
        DBTableBase *table = server->database()->FindTable("inet.0");
        table->Enqueue(&request);

        task_util::WaitForIdle();
    }

    void DeleteRoute(BgpServerTest *server, const char *net, int prefixlen) {
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        boost::system::error_code err;
        Ip4Address addr = Ip4Address::from_string(net, err);
        Ip4Prefix prefix(addr, prefixlen);
        request.key.reset(new InetTable::RequestKey(prefix, NULL));
        DBTableBase *table = server->database()->FindTable("inet.0");
        table->Enqueue(&request);

        task_util::WaitForIdle();
    }

    const BgpAttr *ExpectRoute(BgpServerTest *server, const char *net,
                               int prefixlen) {
        boost::system::error_code err;
        Ip4Address addr = Ip4Address::from_string(net, err);
        Ip4Prefix prefix(addr, prefixlen);
        InetTable::RequestKey key(prefix, NULL);
        DBEntry *entry = NULL;

        for (int attempt = 0; attempt < task_util_retry_count(); ++attempt) {
            task_util::WaitForIdle();
            BgpTable *table = static_cast<BgpTable *>(
                server->database()->FindTable("inet.0"));
            entry = table->Find(&key);
            if (entry != NULL) {
                BgpRoute *route = static_cast<BgpRoute *>(entry);
                const BgpPath *path = route->BestPath();
                if (path == NULL) {
                    return NULL;
                }
                return path->GetAttr();
            }
            usleep(task_util_wait_time());
        }
        return NULL;
    }

    EventManager evm_;
    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<BgpServerTest> rr_server_;
    std::vector<BgpServerTest *> peers_;
    pugi::xml_document xconfig_;
    pugi::xml_node router_;
};

/*
 * TODO:
 * iBGP route not reflected to non-client
 * originator id detection; originator id is not overwritten
 * cluster list loop detection
 * cluster list append check
 * non-clients do not receive reflected routes
 * clients receive reflected routes with expected attributes
 */
TEST_F(BgpReflectorTest, ReflectClientRoute) {
    thread_->Start();
    ConfigureBase();
    ApplyConfig();
    SetClusterId("client1", "0.0.0.1");
    SetClusterId("client2", "0.0.0.1");

    WaitForEstablished();

    BgpServerTest *client1 = ServerFind("client1");
    ASSERT_TRUE(client1 != NULL);
    GenerateRoute(client1, "10.0.0.0", 24);

    BgpServerTest *client2 = ServerFind("client2");
    const BgpAttr *attr1 = ExpectRoute(client2, "10.0.0.0", 24);
    EXPECT_TRUE(attr1 != NULL);
    // MUST have originator-id and cluster list

    BgpServerTest *peer = ServerFind("meshpeer");
    const BgpAttr *attr2 = ExpectRoute(peer, "10.0.0.0", 24);
    EXPECT_TRUE(attr2 != NULL);
    // MUST have originator-id but not cluster list

    DeleteRoute(client1, "10.0.0.0", 24);
}

static void SetUp() {
    bgp_log_test::init();
    BgpServerTest::GlobalSetUp();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
