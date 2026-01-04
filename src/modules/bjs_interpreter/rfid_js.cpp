// Author: Senape3000
// More info: https://github.com/Senape3000/firmware/blob/main/docs_custom/JS_RFID/RFID_API_README.md

#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)

#include "rfid_js.h"
#include "globals.h"
#include "helpers_js.h"
#include "modules/rfid/tag_o_matic.h"

static TagOMatic *g_tagReader = nullptr;

static TagOMatic *getTagReader() {
    if (!g_tagReader) {
        g_tagReader = new TagOMatic(true); // headless mode
    }
    return g_tagReader;
}

static void clearTagReader() {
    if (g_tagReader) {
        delete g_tagReader;
        g_tagReader = nullptr;
    }
}

duk_ret_t putPropRFIDFunctions(duk_context *ctx, duk_idx_t obj_idx, uint8_t magic) {
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "read", native_rfidRead, 1, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "readUID", native_rfidReadUID, 1, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "write", native_rfidWrite, 1, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "save", native_rfidSave, 1, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "load", native_rfidLoad, 1, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "clear", native_rfidClear, 0, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "addMifareKey", native_rfid_AddMifareKey, 1, magic);
    return 0;
}

duk_ret_t registerRFID(duk_context *ctx) {
    bduk_register_c_lightfunc(ctx, "rfidRead", native_rfidRead, 1);
    bduk_register_c_lightfunc(ctx, "rfidReadUID", native_rfidReadUID, 1);
    bduk_register_c_lightfunc(ctx, "rfidWrite", native_rfidWrite, 1);
    bduk_register_c_lightfunc(ctx, "rfidSave", native_rfidSave, 1);
    bduk_register_c_lightfunc(ctx, "rfidLoad", native_rfidLoad, 1);
    bduk_register_c_lightfunc(ctx, "rfidClear", native_rfidClear, 0);
    bduk_register_c_lightfunc(ctx, "rfidAddMifareKey", native_rfid_AddMifareKey, 1);
    return 0;
}

duk_ret_t native_rfidRead(duk_context *ctx) {
    // usage: rfidRead(timeout_in_seconds : number = 10);
    // returns: object with complete or null data on timeout

    duk_int_t timeout = duk_get_int_default(ctx, 0, 10);

    TagOMatic *tagReader = getTagReader();

    // Use existing headless functionality
    String jsonResult = tagReader->read_tag_headless(timeout);

    if (jsonResult.isEmpty()) {
        duk_push_null(ctx);
        return 1;
    }

    // Create a JS object from the fields
    duk_idx_t obj_idx = duk_push_object(ctx);

    // Extract fields from the interface
    RFIDInterface *rfid = tagReader->getRFIDInterface();

    if (rfid) {
        duk_push_string(ctx, rfid->printableUID.uid.c_str());
        duk_put_prop_string(ctx, obj_idx, "uid");

        duk_push_string(ctx, rfid->printableUID.picc_type.c_str());
        duk_put_prop_string(ctx, obj_idx, "type");

        duk_push_string(ctx, rfid->printableUID.sak.c_str());
        duk_put_prop_string(ctx, obj_idx, "sak");

        duk_push_string(ctx, rfid->printableUID.atqa.c_str());
        duk_put_prop_string(ctx, obj_idx, "atqa");

        duk_push_string(ctx, rfid->printableUID.bcc.c_str());
        duk_put_prop_string(ctx, obj_idx, "bcc");

        duk_push_string(ctx, rfid->strAllPages.c_str());
        duk_put_prop_string(ctx, obj_idx, "pages");

        duk_push_int(ctx, rfid->totalPages);
        duk_put_prop_string(ctx, obj_idx, "totalPages");
    }

    return 1;
}

duk_ret_t native_rfidReadUID(duk_context *ctx) {
    // usage: rfidReadUID(timeout_in_seconds : number = 5);
    // returns: string (UID) or empty string on timeout

    duk_int_t timeout = duk_get_int_default(ctx, 0, 5);

    TagOMatic tagReader(true);

    String uid = tagReader.read_uid_headless(timeout);
    duk_push_string(ctx, uid.c_str());

    return 1;
}

duk_ret_t native_rfidWrite(duk_context *ctx) {
    // usage: rfidWrite(timeout_in_seconds : number = 10);
    // returns: { success: boolean, message: string }

    duk_int_t timeout = duk_get_int_default(ctx, 0, 10);

    TagOMatic *tagReader = getTagReader();

    // Use headless write function
    int result = tagReader->write_tag_headless(timeout);

    // Create return object
    duk_idx_t obj_idx = duk_push_object(ctx);

    switch (result) {
        case RFIDInterface::SUCCESS:
            duk_push_boolean(ctx, true);
            duk_put_prop_string(ctx, obj_idx, "success");
            duk_push_string(ctx, "Tag written successfully");
            duk_put_prop_string(ctx, obj_idx, "message");
            break;

        case RFIDInterface::TAG_NOT_PRESENT:
            duk_push_boolean(ctx, false);
            duk_put_prop_string(ctx, obj_idx, "success");
            duk_push_string(ctx, "No tag present");
            duk_put_prop_string(ctx, obj_idx, "message");
            break;

        case RFIDInterface::TAG_NOT_MATCH:
            duk_push_boolean(ctx, false);
            duk_put_prop_string(ctx, obj_idx, "success");
            duk_push_string(ctx, "Tag types do not match");
            duk_put_prop_string(ctx, obj_idx, "message");
            break;

        default:
            duk_push_boolean(ctx, false);
            duk_put_prop_string(ctx, obj_idx, "success");
            duk_push_string(ctx, "Error writing data to tag");
            duk_put_prop_string(ctx, obj_idx, "message");
            break;
    }

    return 1;
}

duk_ret_t native_rfidSave(duk_context *ctx) {
    // usage: rfidSave(filename : string);
    // returns: { success: boolean, message: string, filepath: string }

    if (!duk_is_string(ctx, 0)) {
        duk_push_null(ctx);
        return 1;
    }

    const char *filename = duk_get_string(ctx, 0);

    TagOMatic *tagReader = getTagReader();

    // Save file
    String result = tagReader->save_file_headless(String(filename));

    // Create return object
    duk_idx_t obj_idx = duk_push_object(ctx);

    if (!result.isEmpty()) {
        duk_push_boolean(ctx, true);
        duk_put_prop_string(ctx, obj_idx, "success");

        duk_push_string(ctx, "File saved successfully");
        duk_put_prop_string(ctx, obj_idx, "message");

        duk_push_string(ctx, result.c_str());
        duk_put_prop_string(ctx, obj_idx, "filepath");
    } else {
        duk_push_boolean(ctx, false);
        duk_put_prop_string(ctx, obj_idx, "success");

        duk_push_string(ctx, "Error saving file");
        duk_put_prop_string(ctx, obj_idx, "message");

        duk_push_string(ctx, "");
        duk_put_prop_string(ctx, obj_idx, "filepath");
    }

    return 1;
}

duk_ret_t native_rfidLoad(duk_context *ctx) {
    // usage: rfidLoad(filename : string);
    // returns: object with tag data or null on error

    if (!duk_is_string(ctx, 0)) {
        duk_push_null(ctx);
        return 1;
    }

    const char *filename = duk_get_string(ctx, 0);

    TagOMatic *tagReader = getTagReader();

    // Load file
    int result = tagReader->load_file_headless(String(filename));

    if (result != RFIDInterface::SUCCESS) {
        duk_push_null(ctx);
        return 1;
    }

    // Get the loaded data
    RFIDInterface *rfid = tagReader->getRFIDInterface();

    if (!rfid) {
        duk_push_null(ctx);
        return 1;
    }

    // Create return object with loaded data
    duk_idx_t obj_idx = duk_push_object(ctx);

    duk_push_string(ctx, rfid->printableUID.uid.c_str());
    duk_put_prop_string(ctx, obj_idx, "uid");

    duk_push_string(ctx, rfid->printableUID.picc_type.c_str());
    duk_put_prop_string(ctx, obj_idx, "type");

    duk_push_string(ctx, rfid->printableUID.sak.c_str());
    duk_put_prop_string(ctx, obj_idx, "sak");

    duk_push_string(ctx, rfid->printableUID.atqa.c_str());
    duk_put_prop_string(ctx, obj_idx, "atqa");

    duk_push_string(ctx, rfid->printableUID.bcc.c_str());
    duk_put_prop_string(ctx, obj_idx, "bcc");

    duk_push_string(ctx, rfid->strAllPages.c_str());
    duk_put_prop_string(ctx, obj_idx, "pages");

    duk_push_int(ctx, rfid->totalPages);
    duk_put_prop_string(ctx, obj_idx, "totalPages");

    duk_push_int(ctx, rfid->dataPages);
    duk_put_prop_string(ctx, obj_idx, "dataPages");

    return 1;
}

duk_ret_t native_rfidClear(duk_context *ctx) {
    // usage: rfidClear();
    // returns: undefined

    clearTagReader();
    return 0;
}

duk_ret_t native_rfid_AddMifareKey(duk_context *ctx) {
    if (!duk_is_string(ctx, 0)) {
        duk_idx_t obj_idx = duk_push_object(ctx);
        duk_push_boolean(ctx, false);
        duk_put_prop_string(ctx, obj_idx, "success");
        duk_push_string(ctx, "Invalid parameter: key must be a string");
        duk_put_prop_string(ctx, obj_idx, "message");
        return 1;
    }

    const char *key = duk_get_string(ctx, 0);
    String keyStr = String(key);

    bruceConfig.addMifareKey(keyStr);

    duk_idx_t obj_idx = duk_push_object(ctx);
    duk_push_boolean(ctx, true);
    duk_put_prop_string(ctx, obj_idx, "success");
    duk_push_string(ctx, "Key processed");
    duk_put_prop_string(ctx, obj_idx, "message");
    duk_push_string(ctx, keyStr.c_str());
    duk_put_prop_string(ctx, obj_idx, "key");

    return 1;
}

#endif
