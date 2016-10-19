#!/usr/bin/python

# Copyright CEA/DAM 2015
# This file is part of the Phobos project

"""
High level, object-oriented interface over DSS. This is the module to use
to interact with phobos DSS layer, as it provides a safe, expressive and
clean API to access it.
"""

import json
import logging
import os.path
from socket import gethostname

import phobos.capi.dss as cdss
import phobos.capi.ldm as cldm


# Valid filter suffix and associated operators.
FILTER_OPERATORS = (
    ('__not', '$NOR'),
    ('__gt', '$GT'),
    ('__ge', '$GTE'),
    ('__lt', '$LT'),
    ('__le', '$LTE'),
    ('__like', '$LIKE'),
    ('__jcontain', '$INJSON'),
    ('__jexist', '$XJSON')
)

OBJECT_PREFIXES = {
    'device': 'DSS::DEV::',
    'extent': 'DSS::EXT::',
    'media':  'DSS::MDA::',
}


class GenericError(BaseException):
    """Base error to described DSS failures."""

def key_convert(obj_type, key):
    """Split key, return actual name and associated operator."""
    kname = key
    comp = None # default (equal)
    kname_prefx = OBJECT_PREFIXES[obj_type] # KeyError on unsupported obj_type
    for sufx, comp_enum in FILTER_OPERATORS:
        if key.endswith(sufx):
            kname, comp = key[:len(sufx):], comp_enum
            break
    return "%s%s" % (kname_prefx, kname), comp

def dss_filter(obj_type, **kwargs):
    """Convert a k/v filter into a CDSS-compatible list of criteria."""
    if len(kwargs) == 0:
        return None
    filter = cdss.dss_filter()
    criteria = []
    for key, val in kwargs.iteritems():
        key, comp = key_convert(obj_type, key)
        if comp is None:
            # Implicit equal
            criteria.append({key: val})
        else:
            criteria.append({comp: {key: val}})

    assert len(criteria) > 0

    if len(criteria) == 1:
        filt_str = json.dumps(criteria[0])
    else:
        filt_str = json.dumps({'$AND': criteria})

    rc = cdss.dss_filter_build(filter, filt_str)
    if rc:
        raise GenericError("Invalid filter criteria")

    return filter

class PhobosHook(object):
    """High level interface for C structures exposed by SWIG."""
    #pylint: disable=not-callable
    src_cls = None
    display_fields = {}

    def __init__(self, inst=None, **kwargs):
        """Initialize new instance of Phobos object wrapper."""
        super(PhobosHook, self).__init__(**kwargs)
        if inst is not None:
            self._inst = inst
        else:
            self._inst = self.src_cls()

    def __getattr__(self, item):
        """Transparently fetch attributes of either the proxified
        object (in priority) or the wrapping instance (fallback)."""
        if not item.startswith("_"):
            rc = getattr(self._inst, item, None)
            if rc is not None:
                return rc
        return super(PhobosHook, self).__getattribute__(item)

    def __setattr__(self, item, value):
        """Transparently set attributes of either the proxified
        object (in priority) or the wrapping instance (fallback)."""
        if hasattr(self, "_inst") and hasattr(self._inst, item):
            self._inst.__setattr__(item, value)
        else:
            super(PhobosHook, self).__setattr__(item, value)

    def todict(self, numeric=False):
        """export object attributs as dict"""
        export = {}
        for key in sorted(self.display_fields.keys()):
            if not numeric and self.display_fields[key]:
                reprt = self.display_fields.get(key, lambda x: x)
                export[key] = reprt(getattr(self._inst, key))
            elif hasattr(self._inst, key):
                export[key] = str(getattr(self._inst, key))
            else:
                export[key] = str(getattr(self, key))
        return export

class CliDevice(PhobosHook):
    """Device python object"""
    src_cls = cdss.dev_info
    shortname = "dev"
    display_fields = {
        'adm_status': cdss.adm_status2str,
        'family': cdss.dev_family2str,
        'host': None,
        'model': None,
        'path': None,
        'serial': None,
        'lock_status': None,
        'lock_ts': None
    }

    @property
    def lock_status(self):
        """ Wrapper to get lock status"""
        return self.lock.lock

    @property
    def lock_ts(self):
        """ Wrapper to get lock timestamp"""
        return self.lock.lock_ts

class CliMedia(PhobosHook):
    """Media python object"""
    src_cls = cdss.media_info
    shortname = "media"
    display_fields = {
        'adm_status': cdss.adm_status2str,
        'fs_status': cdss.fs_status2str,
        'fs_type': cdss.fs_type2str,
        'addr_type': None,
        'model': None,
        'ident': None,
        'lock_status': None,
        'lock_ts': None
    }

    def todict(self, numeric=False):
        """export media attribts as dict"""
        export = super(CliMedia, self).todict()
        export.update(self.stats)
        return export

    @property
    def ident(self):
        """ Wrapper to get media id  """
        return cdss.media_id_get(self._inst.id)

    @property
    def lock_status(self):
        """ Wrapper to get lock status"""
        return self.lock.lock

    @property
    def lock_ts(self):
        """ Wrapper to get lock timestamp"""
        return self.lock.lock_ts


    @property
    def stats(self):
        """ Wrapper to get media stats as dict """
        stats = {
            'stats.nb_obj': self._inst.stats.nb_obj,
            'stats.logc_spc_used': self._inst.stats.logc_spc_used,
            'stats.phys_spc_used': self._inst.stats.phys_spc_used,
            'stats.phys_spc_free': self._inst.stats.phys_spc_free,
            'stats.nb_load': self._inst.stats.nb_load,
            'stats.nb_errors': self._inst.stats.nb_errors,
            'stats.last_load': self._inst.stats.last_load,
        }
        return stats

class ObjectManager(object):
    """Proxy to manipulate (CRUD) objects in DSS."""
    def __init__(self, obj_type, client, **kwargs):
        """Initialize new instance."""
        super(ObjectManager, self).__init__(**kwargs)
        self.logger = logging.getLogger(__name__)
        self.obj_type = obj_type
        self.client = client

    def get(self, **kwargs):
        """Retrieve objects from DSS."""
        method = getattr(cdss, 'dss_%s_get' % self.obj_type)
        return method(self.client.handle, dss_filter(self.obj_type, **kwargs))

    def insert(self, objects):
        """Insert objects into DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        return method(self.client.handle, objects, cdss.DSS_SET_INSERT)

    def update(self, objects):
        """Update objects in DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        return method(self.client.handle, objects, cdss.DSS_SET_UPDATE)

    def delete(self, objects):
        """Delete objects from DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        return method(self.client.handle, objects, cdss.DSS_SET_DELETE)


class DeviceManager(ObjectManager):
    """Proxy to manipulate devices."""
    def add(self, device_type, device_path, locked=True):
        """Query device and insert information into DSS."""
        real_path = os.path.realpath(device_path)

        # We could use swig typemaps tricks to return a dev_adapter directly
        # from cldm.get_dev_adapter() but this is simpler and only done here.
        dev_adapter = cldm.dev_adapter()
        rc = cldm.get_dev_adapter(device_type, dev_adapter)
        if rc:
            self.logger.error('Cannot get device adapter')
            return rc

        # Idem typemaps.
        dev_state = cldm.ldm_dev_state()
        rc = cldm.ldm_dev_query(dev_adapter, real_path, dev_state)
        if rc:
            self.logger.error("Cannot query device '%s'" % device_path)
            return rc

        dev_info = cdss.dev_info()
        dev_info.family = dev_state.lds_family
        dev_info.model = dev_state.lds_model
        dev_info.path = device_path
        dev_info.host = gethostname().split('.')[0]
        dev_info.serial = dev_state.lds_serial
        if locked:
            dev_info.adm_status = cdss.PHO_DEV_ADM_ST_LOCKED
        else:
            dev_info.adm_status = cdss.PHO_DEV_ADM_ST_UNLOCKED

        rc = self.insert([dev_info])
        if rc != 0:
            self.logger.error("Cannot insert dev info for '%s'" % device_path)
            return rc

        self.logger.info("Device '%s:%s' successfully added: " \
                         "model=%s serial=%s (%s)",
                         dev_info.host, device_path, dev_info.model,
                         dev_info.serial, locked and "locked" or "unlocked")
        return 0

    def get(self, **kwargs):
        """Retrieve objects from DSS."""
        method = getattr(cdss, 'dss_%s_get' % self.obj_type)
        devices = method(self.client.handle,
                         dss_filter(self.obj_type, **kwargs))
        return [CliDevice(x) for x in devices]

    def update(self, objects):
        """Update objects in DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        devices = [x._inst for x in objects]
        return method(self.client.handle, devices, cdss.DSS_SET_UPDATE)

    def delete(self, objects):
        """Delete objects in DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        devices = [x._inst for x in objects]
        return method(self.client.handle, devices, cdss.DSS_SET_DELETE)

class MediaManager(ObjectManager):
    """Proxy to manipulate media."""
    def add(self, mtype, fstype, model, label, locked=False):
        """Insert media into DSS."""
        media = cdss.media_info()
        media.id.type = mtype
        media.fs_type = cdss.str2fs_type(fstype)
        media.model = model
        media.addr_type = cdss.PHO_ADDR_HASH1
        if locked:
            media.adm_status = cdss.PHO_MDA_ADM_ST_LOCKED
        else:
            media.adm_status = cdss.PHO_MDA_ADM_ST_UNLOCKED
        media.stats = cdss.media_stats()
        cdss.media_id_set(media.id, label)

        rc = self.insert([media])
        if rc != 0:
            self.logger.error("Cannot insert media info for '%s'" % label)
            return rc

        self.logger.debug("Media '%s' successfully added: "\
                          "model=%s fs=%s (%s)",
                          label, model, fstype,
                          locked and "locked" or "unlocked")
        return 0

    def get(self, **kwargs):
        """Retrieve objects from DSS."""
        method = getattr(cdss, 'dss_%s_get' % self.obj_type)
        media = method(self.client.handle, dss_filter(self.obj_type, **kwargs))
        return [CliMedia(x) for x in media]

    def update(self, objects):
        """Update objects in DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        media = [x._inst for x in objects]
        return method(self.client.handle, media, cdss.DSS_SET_UPDATE)

    def delete(self, objects):
        """Delete objects in DSS"""
        method = getattr(cdss, 'dss_%s_set' % self.obj_type)
        devices = [x._inst for x in objects]
        return method(self.client.handle, devices, cdss.DSS_SET_DELETE)

class Client(object):
    """Main class: issue requests to the DSS and format replies."""
    def __init__(self, **kwargs):
        """Initialize a new DSS context."""
        super(Client, self).__init__(**kwargs)
        self.handle = None
        self.media = MediaManager('media', self)
        self.devices = DeviceManager('device', self)
        self.extents = ObjectManager('extent', self)
        self.objects = ObjectManager('object', self)

    def connect(self, **kwargs):
        """ Establish a fresh connection or renew a stalled one if needed."""
        if self.handle is not None:
            self.disconnect()

        self.handle = cdss.dss_handle()
        rcode = cdss.dss_init(self.handle)
        if rcode != 0:
            raise GenericError('DSS initialization failed')

    def disconnect(self):
        """Disconnect from DSS and reset handle."""
        if self.handle is not None:
            cdss.dss_fini(self.handle)
            self.handle = None
