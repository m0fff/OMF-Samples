This example sends real-time accelerometer data from an IoT dev kit into OCS.  To run this example, obtain a Particle Internet Button dev kit (https://store.particle.io/ or (https://www.amazon.com/Particle-Internet-Button-Accelerometer-Buttons/dp/B01L46Q9LC).  After registering that Particle Photon IoT device (see https://docs.particle.io/guide/getting-started/start/photon/#connect-your-photon) with your Particle account (create a free account here: https://login.particle.io/signup), sign into the online Particle IDE (https://login.particle.io/login) and create a new App, and paste into that app all of the code in this example .ino file.  Save the code, but before running it, in a new browser tab, open the online Particle Console (https://console.particle.io/integrations) and in the Integrations section create three Webhook integrations using the three Webhook JSON templates found at the end of this script (see https://docs.particle.io/guide/tools-and-features/webhooks/#custom-template).  After creating and saving those three Webhooks, you may flash the code to your device, and data will be sent to OCS.