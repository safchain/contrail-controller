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
