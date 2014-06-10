/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "service_instance.h"

#include "ifmap/ifmap_node.h"
#include "schema/vnc_cfg_types.h"

#include "oper/ifmap_dependency_manager.h"
#include "oper/operdb_init.h"
#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <cmn/agent_param.h>

using boost::uuids::uuid;

#define OTHER_TYPE  "Other"

/*
 * ServiceInstanceTable create requests contain the IFMapNode that this
 * entry corresponds to.
 */
class ServiceInstanceCreate : public AgentData {
  public:
    ServiceInstanceCreate(IFMapNode *node) :
            node_(node) {
    }
    IFMapNode *node() { return node_; }

  private:
    IFMapNode *node_;
};

/*
 * ServiceInstanceTable update requests contain ServiceInstance Properties.
 */
class ServiceInstanceUpdate : public AgentData {
  public:
    typedef ServiceInstance::Properties Properties;
    ServiceInstanceUpdate(Properties &properties) :
            properties_(properties) {
    }
    const Properties &properties() { return properties_; }

  private:
    Properties properties_;
};

class ServiceInstanceTypesMapping {
public:
    static ServiceInstance::ServiceType StrServiceTypeToInt(const std::string &type);
    static std::string IntServiceTypeToStr(const ServiceInstance::ServiceType &type);
    static ServiceInstance::VirtualizationType StrVirtualizationTypeToInt(const std::string &type);

private:
    typedef std::map<std::string, int> StrTypeToIntMap;
    typedef std::pair<std::string, int> StrTypeToIntPair;
    static StrTypeToIntMap service_type_map_;
    static StrTypeToIntMap virtualization_type_map_;

    static StrTypeToIntMap InitServiceTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("source-nat", ServiceInstance::SourceNAT));
        types.insert(StrTypeToIntPair("load-balancer", ServiceInstance::LoadBalancer));

        return types;
    };

    static StrTypeToIntMap InitVirtualizationTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("virtual-machine", ServiceInstance::VirtualMachine));
        types.insert(StrTypeToIntPair("network-namespace", ServiceInstance::NetworkNamespace));

        return types;
    };
};

class IFMapNodeLookup {
public:
    IFMapNodeLookup(IFMapNode *node, IFMapTable *table);

    class type_iterator : public boost::iterator_facade<
        type_iterator,
        DBGraphVertex,
        boost::forward_traversal_tag> {
    public:
        type_iterator();
        type_iterator(DBGraph *graph, IFMapNode *node, IFMapTable *table);

     private:
        friend class boost::iterator_core_access;

        void seek();
        void increment();
        bool equal(const type_iterator &rhs) const;
        IFMapNode &dereference() const;

        DBGraph *graph_;
        IFMapTable *table_;
        DBGraphVertex::adjacency_iterator iter_;
        DBGraphVertex::adjacency_iterator end_;
    };

    type_iterator begin() {
        return type_iterator(graph_, node_, table_);
    }
    type_iterator end() {
        return type_iterator();
    }

    IFMapNode *first();

private:
    IFMapNode *node_;
    IFMapTable *table_;
    DBGraph *graph_;
};

static uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static void ExecCmd(std::string cmd) {
    /*
     * TODO(safchain) start async process
     */
    std::cout << "Start NetNS Script: " << cmd << std::endl;
}

/*
 * Walks through the graph starting from the service instance in order to
 * find the Virtual Machine associated. Set the vm_id of the ServiceInstanceData
 * object and return the VM node.
 */
static IFMapNode *FindAndSetVirtualMachine(Agent *agent,
        ServiceInstance::Properties *properties, IFMapNode *si_node) {
    IFMapNodeLookup *il = new IFMapNodeLookup(si_node, agent->cfg()->cfg_vm_table());
    IFMapNode *vm_node = il->first();
    if (vm_node == NULL) {
        return NULL;
    }

    autogen::VirtualMachine *vm =
            static_cast<autogen::VirtualMachine *>(vm_node->GetObject());
    properties->instance_id = IdPermsGetUuid(vm->id_perms());

    return vm_node;
}

static void FindAndSetVirtualNetworks(Agent *agent, ServiceInstance::Properties *properties,
        IFMapNode *vm_node, const std::string &left,
        const std::string &right) {
    AgentConfig *config = agent->cfg();

    /*
     * Lookup for VMI nodes
     */
    IFMapNodeLookup *il_vmi = new IFMapNodeLookup(vm_node, config->cfg_vm_interface_table());
    for (IFMapNodeLookup::type_iterator vmi_iter = il_vmi->begin();
         vmi_iter != il_vmi->end(); ++vmi_iter) {
        IFMapNode *vmi_node = static_cast<IFMapNode *>(vmi_iter.operator->());

        /*
         * Then Lookup for VN nodes
         */
        IFMapNodeLookup *il_vn = new IFMapNodeLookup(vmi_node, config->cfg_vn_table());
        for (IFMapNodeLookup::type_iterator vn_iter = il_vn->begin();
             vn_iter != il_vn->end(); ++vn_iter) {
            IFMapNode *vn_node = static_cast<IFMapNode *>(vn_iter.operator->());

            autogen::VirtualNetwork *vn =
                static_cast<autogen::VirtualNetwork *>(vn_node->GetObject());
            if (left.compare(vn_node->name()) == 0) {
                properties->vmi_inside = IdPermsGetUuid(vn->id_perms());
            } else if (right.compare(vn_node->name()) == 0) {
                properties->vmi_outside = IdPermsGetUuid(vn->id_perms());
            }
        }
    }
}

/*
 * Walks through the graph in order to get the template associated to the
 * Service Instance Node and set the types in the ServiceInstanceData object.
 */
static void FindAndSetTypes(Agent *agent, ServiceInstance::Properties *properties, IFMapNode *si_node) {
    IFMapNodeLookup *il = new IFMapNodeLookup(si_node, agent->cfg()->cfg_service_template_table());
    IFMapNode *st_node = il->first();
    if (st_node == NULL) {
        return;
    }

    autogen::ServiceTemplate *svc_template =
            static_cast<autogen::ServiceTemplate *>(st_node->GetObject());
    autogen::ServiceTemplateType svc_template_props = svc_template->properties();

    ServiceInstance::ServiceType service_type = ServiceInstanceTypesMapping::StrServiceTypeToInt(
            svc_template_props.service_type);
    properties->service_type = service_type;

    /*
    * TODO(safchain) waiting for the edouard's patch merge
    */
    /*int virtualization_type = ServiceInstanceTypeMapping::StrServiceTypeToInt(
       svc_template_props.service_virtualization_type);*/
    ServiceInstance::VirtualizationType virtualization_type = ServiceInstance::NetworkNamespace;
    properties->virtualization_type = virtualization_type;
}

void ServiceInstance::Properties::Clear() {
    service_type = 0;
    virtualization_type = 0;
    instance_id = boost::uuids::nil_uuid();
    vmi_inside = boost::uuids::nil_uuid();
    vmi_outside = boost::uuids::nil_uuid();
}

template <typename Type>
static int compare(const Type &lhs, const Type &rhs) {
    if (lhs < rhs) {
        return -1;
    }
    if (rhs < lhs) {
        return 1;
    }
    return 0;
}

int ServiceInstance::Properties::CompareTo(const Properties &rhs) const {
    int cmp = 0;
    cmp = compare(service_type, rhs.service_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(virtualization_type, rhs.virtualization_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(instance_id, rhs.instance_id);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vmi_inside, rhs.vmi_inside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vmi_outside, rhs.vmi_outside);
    if (cmp != 0) {
        return cmp;
    }
    return cmp;
}

/*
 * ServiceInstance class
 */
ServiceInstance::ServiceInstance() {
    properties_.Clear();
}

bool ServiceInstance::IsLess(const DBEntry &rhs) const {
    const ServiceInstance &si = static_cast<const ServiceInstance &>(rhs);
    return uuid_ < si.uuid_;
}

std::string ServiceInstance::ToString() const {
    std::stringstream uuid_str;
    uuid_str << uuid_;
    return uuid_str.str();
}

void ServiceInstance::SetKey(const DBRequestKey *key) {
    const ServiceInstanceKey *si_key =
            static_cast<const ServiceInstanceKey *>(key);
    uuid_ = si_key->instance_id();
}

DBEntryBase::KeyPtr ServiceInstance::GetDBRequestKey() const {
    ServiceInstanceKey *key = new ServiceInstanceKey(uuid_);
    return KeyPtr(key);
}

bool ServiceInstance::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    return false;
}

void ServiceInstance::StartNetworkNamespace(bool restart) {
    std::stringstream cmd_str;

    Agent *agent = Agent::GetInstance();

    std::string cmd = agent->params()->si_netns_command();
    if (cmd.length() == 0) {
        LOG(DEBUG, "Path for network namespace service instance not specified"
                "in the config file");
        return;
    }
    cmd_str << cmd;

    if (restart) {
        cmd_str << " restart ";
    }
    else {
        cmd_str << " start ";
    }

    ServiceInstance::Properties props = properties();

    cmd_str << " --instance_id " << UuidToString(props.instance_id);
    cmd_str << " --vmi_inside " << UuidToString(props.vmi_inside);
    cmd_str << " --vmi_outside " << UuidToString(props.vmi_outside);
    cmd_str << " --service_type " << ServiceInstanceTypesMapping::IntServiceTypeToStr(
            static_cast<ServiceType>(props.service_type));

    ExecCmd(cmd_str.str());
}

void ServiceInstance::StopNetworkNamespace() {
    std::stringstream cmd_str;

    Agent *agent = Agent::GetInstance();

    std::string cmd = agent->params()->si_netns_command();
    if (cmd.length() == 0) {
        LOG(DEBUG, "Path for network namespace service instance not specified"
                "in the config file");
        return;
    }
    cmd_str << cmd;

    ServiceInstance::Properties props = properties();

    cmd_str << " stop ";
    cmd_str << " --instance_id " << UuidToString(props.instance_id);

    ExecCmd(cmd_str.str());
}

void ServiceInstance::CalculateProperties(Properties *properties) {
    Agent *agent = Agent::GetInstance();

    autogen::ServiceInstance *svc_instance =
                 static_cast<autogen::ServiceInstance *>(node()->GetObject());
    autogen::ServiceInstanceType si_properties = svc_instance->properties();

    IFMapNode *vm_node = FindAndSetVirtualMachine(agent, properties, node());
    if (vm_node == NULL) {
        return;
    }

    FindAndSetVirtualNetworks(agent, properties, vm_node,
            si_properties.left_virtual_network,
            si_properties.right_virtual_network);

    FindAndSetTypes(agent, properties, node());
}

/*
 * ServiceInstanceTable class
 */
ServiceInstanceTable::ServiceInstanceTable(DB *db, const std::string &name)
        : AgentDBTable(db, name) {
}

std::auto_ptr<DBEntry> ServiceInstanceTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new ServiceInstance());
    entry->SetKey(key);
    return entry;
}

DBEntry *ServiceInstanceTable::Add(const DBRequest *request) {
    ServiceInstance *svc_instance = new ServiceInstance();
    svc_instance->SetKey(request->key.get());
    ServiceInstanceCreate *data =
            static_cast<ServiceInstanceCreate *>(request->data.get());
    svc_instance->set_node(data->node());
    IFMapDependencyManager *manager = agent()->oper_db()->dependency_manager();
    manager->SetObject(data->node(), svc_instance);

    svc_instance->StartNetworkNamespace(false);

    return svc_instance;
}

void ServiceInstanceTable::Delete(DBEntry *entry, const DBRequest *request) {
    ServiceInstance *svc_instance  = static_cast<ServiceInstance *>(entry);
    IFMapDependencyManager *manager = agent()->oper_db()->dependency_manager();
    manager->ResetObject(svc_instance->node());

    svc_instance->StopNetworkNamespace();
}

bool ServiceInstanceTable::OnChange(DBEntry *entry, const DBRequest *request) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);
    ServiceInstanceUpdate *data =
            static_cast<ServiceInstanceUpdate *>(request->data.get());
    svc_instance->set_properties(data->properties());
    return true;
}

void ServiceInstanceTable::Initialize(Agent *agent) {
    set_agent(agent);
    IFMapDependencyManager *manager = agent->oper_db()->dependency_manager();

    manager->Register(
        "service-instance",
        boost::bind(&ServiceInstanceTable::ChangeEventHandler, this, _1));
}

bool ServiceInstanceTable::IFNodeToReq(IFMapNode *node, DBRequest &request) {
    autogen::ServiceInstance *svc_instance =
            static_cast<autogen::ServiceInstance *>(node->GetObject());
    autogen::IdPermsType id = svc_instance->id_perms();
    request.key.reset(new ServiceInstanceKey(IdPermsGetUuid(id)));
    if (!node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.data.reset(new ServiceInstanceCreate(node));
    } else {
        request.oper = DBRequest::DB_ENTRY_DELETE;
    }
    return true;
}

void ServiceInstanceTable::ChangeEventHandler(DBEntry *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);
    ServiceInstance::Properties properties;
    svc_instance->CalculateProperties(&properties);
    if (properties.CompareTo(svc_instance->properties()) != 0) {
        std::auto_ptr<DBRequest> request(new DBRequest());
        request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request->data.reset(new ServiceInstanceUpdate(properties));
        Enqueue(request.release());

        svc_instance->StartNetworkNamespace(true);
    }
}
 
DBTableBase *ServiceInstanceTable::CreateTable(
    DB *db, const std::string &name) {
    ServiceInstanceTable *table = new ServiceInstanceTable(db, name);
    table->Init();
    return table;
}

/*
 * ServiceInstanceTypeMapping class
 */
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::service_type_map_ = InitServiceTypeMap();
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::virtualization_type_map_ = InitVirtualizationTypeMap();

ServiceInstance::ServiceType ServiceInstanceTypesMapping::StrServiceTypeToInt(
        const std::string &type) {
    StrTypeToIntMap::const_iterator it = service_type_map_.find(type);
    if (it != service_type_map_.end()) {
        return static_cast<ServiceInstance::ServiceType>(it->second);
    }
    return ServiceInstance::Other;
}

ServiceInstance::VirtualizationType ServiceInstanceTypesMapping::StrVirtualizationTypeToInt(
        const std::string &type) {
    StrTypeToIntMap::const_iterator it = virtualization_type_map_.find(type);
    if (it != virtualization_type_map_.end()) {
        return static_cast<ServiceInstance::VirtualizationType>(it->second);
    }
    return ServiceInstance::VirtualMachine;
}

std::string ServiceInstanceTypesMapping::IntServiceTypeToStr(const ServiceInstance::ServiceType &type) {
    StrTypeToIntMap::const_iterator it = service_type_map_.begin();
    if (it != service_type_map_.end()) {
        if (it->second == type) {
            return it->first;
        }
    }
    return OTHER_TYPE;
}

/*
 * IFMapNodeLookup class
 */
IFMapNodeLookup::IFMapNodeLookup(IFMapNode *node, IFMapTable *table) : node_(node), table_(table) {
    IFMapAgentTable *t = static_cast<IFMapAgentTable *>(node_->table());
    graph_ = t->GetGraph();
}

IFMapNode *IFMapNodeLookup::first() {
   for (type_iterator it = begin(); it != end(); ++it) {
       return static_cast<IFMapNode *>(it.operator->());
   }

   return NULL;
}

IFMapNodeLookup::type_iterator::type_iterator() : graph_(NULL), table_(NULL) {
}

IFMapNodeLookup::type_iterator::type_iterator(DBGraph *graph, IFMapNode *node, IFMapTable *table) :
    graph_(graph), table_(table) {
    iter_ = node->begin(graph);
    end_ = node->end(graph);

    seek();
}

IFMapNode &IFMapNodeLookup::type_iterator::dereference() const {
    IFMapNode *adj_node = static_cast<IFMapNode *>(iter_.operator->());
    return *adj_node;
}

bool IFMapNodeLookup::type_iterator::equal(const type_iterator &rhs) const {
    if (graph_ == NULL) {
        return (rhs.graph_ == NULL);
    }
    if (rhs.graph_ == NULL) {
        return iter_ == end_;
    }
    return iter_ == rhs.iter_;
}

void IFMapNodeLookup::type_iterator::increment() {
    ++iter_;
    seek();
}

void IFMapNodeLookup::type_iterator::seek() {
    for (; iter_ != end_; ++iter_) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter_.operator->());
        if (adj_node->table() == table_) {
            return;
        }
    }
}
