#include "ifmap/test/ifmap_test_util.h"

#include "db/db.h"
#include "ifmap/ifmap_server_table.h"

namespace ifmap_test_util {

using std::string;

void IFMapLinkCommon(DBRequest *request,
                     const string &lhs, const string &lid,
                     const string &rhs, const string &rid,
                     const string &metadata, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = lhs;
    key->id_name = lid;
    key->id_seq_num = sequence_number;
    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = metadata;
    data->id_type = rhs;
    data->id_name = rid;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
}

void IFMapNodeCommon(IFMapTable *table, DBRequest *request, const string &type,
                     const string &id, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
}

void IFMapPropertyCommon(DBRequest *request, const string &type,
                         const string &id, const string &metadata,
                         AutogenProperty *content, uint64_t sequence_number) {
    IFMapTable::RequestKey *key = new IFMapTable::RequestKey();
    request->key.reset(key);
    key->id_type = type;
    key->id_name = id;
    key->id_seq_num = sequence_number;

    IFMapServerTable::RequestData *data = new IFMapServerTable::RequestData();
    request->data.reset(data);
    data->metadata = metadata;
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
    if (content != NULL) {
        data->content.reset(content);
    }
}

}
