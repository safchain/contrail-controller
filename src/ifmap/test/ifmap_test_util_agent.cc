#include "ifmap/test/ifmap_test_util.h"

#include <map>

#include "db/db.h"
#include "ifmap/ifmap_agent_table.h"

namespace ifmap_test_util {

using std::string;

void IFMapLinkCommon(DBRequest *request,
                     const string &lhs, const string &lid,
                     const string &rhs, const string &rid,
                     const string &metadata, uint64_t sequence_number) {

    IFMapAgentLinkTable::RequestKey *key =
            new IFMapAgentLinkTable::RequestKey();
    request->key.reset(key);
    key->left_key.id_name = lid;
    key->left_key.id_type = lhs;
    key->left_key.id_seq_num = sequence_number;

    key->right_key.id_name = rid;
    key->right_key.id_type = rhs;
    key->right_key.id_seq_num = sequence_number;
}

void IFMapMsgLink(DB *db, const string &ltype, const string &lid,
                  const string &rtype, const string &rid,
                  const string &metadata, uint64_t sequence_number) {
    std::auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    IFMapLinkCommon(request.get(), ltype, lid, rtype, rid, metadata,
                    sequence_number);
    DBTable *link_table = static_cast<DBTable *>(
        db->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(link_table != NULL);
    link_table->Enqueue(request.get());
}

void IFMapMsgUnlink(DB *db, const string &lhs, const string &lid,
                    const string &rhs, const string &rid,
                    const string &metadata) {
    std::auto_ptr<DBRequest> request(new DBRequest());
    request->oper = DBRequest::DB_ENTRY_DELETE;
    IFMapLinkCommon(request.get(), lhs, lid, rhs, rid, metadata, 0);

    DBTable *link_table = static_cast<DBTable *>(
        db->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(link_table != NULL);
    link_table->Enqueue(request.get());
}

void IFMapNodeCommon(IFMapTable *table, DBRequest *request, const string &type,
                     const string &id, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapAgentTable::IFMapAgentData *data =
            new IFMapAgentTable::IFMapAgentData();
    request->data.reset(data);
    data->content.reset(table->AllocObject());
}

void IFMapPropertyCommon(DBRequest *request, const string &type,
                         const string &id, const string &metadata,
                         AutogenProperty *content, uint64_t sequence_number) {
    // TODO:(roque)
}

}
