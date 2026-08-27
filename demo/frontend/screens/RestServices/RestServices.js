
/* global $$, Server */

'use strict';

(async function () {

    $$('call-button').onclick(async function () {
        $$('input-3').setValue('');
        let data = {
            num1: $$('input-1').getValue(),
            num2: $$('input-2').getValue()
        };
        let res = await Server.call('services/MyService', 'addNumbers', data);
        if (res._Success) {
            $$('input-3').setValue(res.num3);
        }


        /* Calling Server.call with await (as above) makes it, essentially, run sequential with other server calls.
           To run it in parallel, do the following instead:

        let data = {
            num1: $$('input-1').getValue(),
            num2: $$('input-2').getValue()
        };
        Server.call('services/MyService', 'addNumbers', data).then(function (res) {
            if (res._Success) {
                $$('input-3').setValue(res.num3);
            }
        });
         */
    });

})();
