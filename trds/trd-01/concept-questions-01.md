Why can't a single read() be assumed to contain a complete HTTP request?
    ANS: Because a full request may be split across packets
What does \r\n\r\n signify and why is it the correct sentinel for "headers are done"?
    ANS: It signifies the end of the headers block (or just the end of a GET request). It's the correct sentinel because every line in the header ends with \r\n, so \r\n\r\n when tokenized becomes an empty line which is interpreted as "client has nothing more to send"

Why use strtok_r() instead of strtok()? What will break later if you use the non-reentrant version?
    ANS: strtok_r() is thread-safe since it relies on the const char* saveptr input to keep track of where the tokenization should start next whereas strtok() has its own internal state which can be malformed by calling it on two different strings. If the non-reentrant version is used then you might have something where you were parsing one request buffer, then on another thread you're parsing a request buffer which mangles the SINGLE saveptr thats stored within strtok().

A header like Host: localhost:9876 has a colon in the value. How does the parser handle this?
    ANS: The parser (that I built) uses strtok_r to parse each line that is a header key: header value pair with the ": " as a delimiter. It only needs to check once, and because strtok_r with \r\n is called beforehand to isolate the line, we don't have to worry about a stray \r\n being left on the line

What happens to the connection if the server receives a partial request and never gets the rest? (Think about what TRD-06 will fix.)
    ANS: If it receives a partial request it will continue to wait for the request to be completed. This holds up the single-threaded server (since it waits for the \r\n\r\n sentinel to arrive).


Why does the test suite send raw bytes via nc for malformed-request tests instead of using curl?
    ANS: it sends raw bytes to see if the server can handle byte-malformed requests (such as non-ascii). For example, it cannot send '\n' or '\r' without sending it as bytes.