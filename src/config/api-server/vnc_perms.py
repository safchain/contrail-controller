#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import json
import string
from provision_defaults import *
from cfgm_common.exceptions import *
from cfgm_common.rest import hdr_server_tenant
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

PERMS_NONE = 0
PERMS_X = 1
PERMS_W = 2
PERMS_R = 4
PERMS_WX = 3
PERMS_RX = 5
PERMS_RW = 6
PERMS_RWX = 7


class VncRequestContext(object):

    def __init__(self, user=None, roles=None, tenant_name=None, auth_open=True):

       self._user = user
       self._roles = roles
       self._auth_open = auth_open
       self._tenant_name = tenant_name

    @property
    def user(self):
        self._user

    @property
    def roles(self):
        self._roles

    @property
    def auth_open(self):
        self._auth_open

    @property
    def tenant_name(self):
        self._tenant_name

    @classmethod
    def from_request(cls, request):
        user = None
        env = request.headers.environ
        if 'HTTP_X_USER' in env:
            user = env['HTTP_X_USER']
        roles = None
        if 'HTTP_X_ROLE' in env:
            roles = env['HTTP_X_ROLE'].split(',')

        app = request.environ['bottle.app']
        auth_open = app.config.auth_open

        tenant_name = env.get(hdr_server_tenant(), 'default-project')

        return VncRequestContext(user, roles, tenant_name, auth_open)


class VncPermissions(object):

    mode_str = {PERMS_R: 'read', PERMS_W: 'write', PERMS_X: 'link'}

    def __init__(self, server_mgr, args):
        self._server_mgr = server_mgr
    # end __init__

    @property
    def _multi_tenancy(self):
        return self._server_mgr._args.multi_tenancy
    # end

    def validate_user_visible_perm(self, id_perms, is_admin):
        return id_perms.get('user_visible', True) is not False or is_admin
    # end

    def validate_perms(self, context, uuid, mode=PERMS_R):
        # retrieve object and permissions
        try:
            id_perms = self._server_mgr._db_conn.uuid_to_obj_perms(uuid)
        except NoIdError:
            return (True, '')

        err_msg = (403, 'Permission Denied')

        is_admin = 'admin' in [x.lower() for x in context.roles]

        owner = id_perms['permissions']['owner']
        group = id_perms['permissions']['group']
        perms = id_perms['permissions']['owner_access'] << 6 | \
            id_perms['permissions']['group_access'] << 3 | \
            id_perms['permissions']['other_access']

        # check perms
        mask = 0
        if context.user == owner:
            mask |= 0700
        if group in context.roles:
            mask |= 0070
        if mask == 0:   # neither user nor group
            mask = 07

        mode_mask = mode | mode << 3 | mode << 6
        ok = is_admin or (mask & perms & mode_mask)

        if ok and mode == PERMS_W:
            ok = self.validate_user_visible_perm(id_perms, is_admin)

        return (True, '') if ok else (False, err_msg)
    # end validate_perms

    # set user/role in object dict from incoming request
    # called from post handler when object is created
    def set_user_role(self, context, obj_dict):
        if context.user:
            obj_dict['id_perms']['permissions']['owner'] = context.user
        if context.roles:
            obj_dict['id_perms']['permissions']['group'] = context.roles[0]
    # end set_user_role

    def check_perms_write(self, context, id):
        if context.auth_open:
            return (True, '')

        if not self._multi_tenancy:
            return (True, '')

        return self.validate_perms(context, id, PERMS_W)
    # end check_perms_write

    def check_perms_read(self, context, id):
        if context.auth_open:
            return (True, '')

        if not self._multi_tenancy:
            return (True, '')

        return self.validate_perms(context, id, PERMS_R)
    # end check_perms_read

    def check_perms_link(self, context, id):
        if context.auth_open:
            return (True, '')

        if not self._multi_tenancy:
            return (True, '')

        return self.validate_perms(context, id, PERMS_X)
    # end check_perms_link
