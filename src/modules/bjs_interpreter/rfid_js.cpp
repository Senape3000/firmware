// Author: Senape3000
// More info: https://github.com/Senape3000/firmware/blob/main/docs_custom/JS_RFID/RFID_API_README.md

#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)

#include "rfid_js.h"
#include "helpers_js.h"
#include "modules/rfid/tag_o_matic.h"

duk_ret_t putPropRFIDFunctions(duk_context *ctx, duk_idx_t obj_idx, uint8_t magic) {
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "read", native_rfidRead, 1, magic);
    bduk_put_prop_c_lightfunc(ctx, obj_idx, "readUID", native_rfidReadUID, 1, magic);
    return 0;
}

duk_ret_t registerRFID(duk_context *ctx) {
    bduk_register_c_lightfunc(ctx, "rfidRead", native_rfidRead, 1);
    bduk_register_c_lightfunc(ctx, "rfidReadUID", native_rfidReadUID, 1);
    return 0;
}

duk_ret_t native_rfidRead(duk_context *ctx) {
    // usage: rfidRead(timeout_in_seconds : number = 10);
    // returns: object with complete or null data on timeout

    duk_int_t timeout = duk_get_int_default(ctx, 0, 10);

    // Use headless constructor (does not launch UI)
    TagOMatic tagReader(true); // true = headless mode

    // Use existing headless functionality
    String jsonResult = tagReader.read_tag_headless(timeout);

    if (jsonResult.isEmpty()) {
        duk_push_null(ctx);
        return 1;
    }

    // Parse JSON manually (the result is already in JSON format)
    // Create a JS object from the fields
    duk_idx_t obj_idx = duk_push_object(ctx);

    // Extract fields from the JSON string (jsonResult already contains all the data)
    // For simplicity, let's pass the interface directly
    RFIDInterface *rfid = tagReader.getRFIDInterface();

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

    // Use headless constructor
    TagOMatic tagReader(true); // true = headless mode

    // Use existing headless function
    String uid = tagReader.read_uid_headless(timeout);

    duk_push_string(ctx, uid.c_str());
    return 1;
}

#endif
