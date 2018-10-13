// you can also attach multiple feeds to the same
// meesage handler function. both counter and counter-two
// are attached to this callback function, and messages
// for both will be received by this function.
void handleMessage(AdafruitIO_Data *data) {
  String receivedValue = String(data->value());  //convert value to a String
  String receivedfeedName = String(data->feedName());  //convert value to a String

  //uncomment transmissions below if desired

    if ( Serial )
  {
    Serial.print("received <- ");

    // since we are using the same function to handle
    // messages for two feeds, we can use feedName() in
    // order to find out which feed the message came from.
    Serial.print(data->feedName());
    Serial.print(" ");

    // print out the received count or counter-two value
    Serial.println(data->value());
  }

}
