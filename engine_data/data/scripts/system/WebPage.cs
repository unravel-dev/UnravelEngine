using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// The page a browser build runs in. A project that ships a page of its own (an index.html among the
    /// profile's Extra Files) offers the game functions and posts it messages through one object,
    /// <c>unravelPage</c>:
    /// <code>
    /// globalThis.unravelPage ??= {};
    /// (unravelPage.functions ??= {}).openStore = (item) => { showStore(item); return 'opened'; };
    /// button.onclick = () => unravelPage.post('store-closed');
    /// </code>
    /// On every other platform there is no page: <see cref="isAvailable"/> is false, calls return null
    /// and no message arrives.
    /// </summary>
    public static class WebPage
    {
        /// <summary>
        /// Whether the game runs in a browser page.
        /// </summary>
        public static bool isAvailable
        {
            get => internal_m2n_web_page_is_available();
        }

        /// <summary>
        /// Calls a function of the page and returns its answer as text.
        /// </summary>
        /// <param name="function">The function's name in <c>unravelPage.functions</c>.</param>
        /// <param name="argument">
        /// The text the function gets. Pass several values as one text, such as JSON.
        /// </param>
        /// <returns>
        /// The answer as text; an empty text when the function answers nothing or throws (the browser's
        /// console says what); null when the page offers no such function or the game is not in a page.
        /// </returns>
        public static string Call(string function, string argument = "")
        {
            if(string.IsNullOrEmpty(function))
            {
                throw new ArgumentException("A page function needs a name.", nameof(function));
            }
            if(!internal_m2n_web_page_has_function(function))
            {
                return null;
            }
            return internal_m2n_web_page_call(function, argument ?? "");
        }

        /// <summary>
        /// Takes the oldest message the page posted with <c>unravelPage.post(text)</c>. Messages wait in
        /// the page until the game takes them, so a script reads them in its update:
        /// <code>
        /// while(WebPage.TryReceive(out string message)) { Handle(message); }
        /// </code>
        /// </summary>
        /// <param name="message">The message's text; null when none waits.</param>
        /// <returns>True when a message was taken.</returns>
        public static bool TryReceive(out string message)
        {
            if(internal_m2n_web_page_get_message_count() <= 0)
            {
                message = null;
                return false;
            }
            message = internal_m2n_web_page_take_message();
            return true;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_web_page_is_available();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_web_page_has_function(string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_web_page_call(string name, string argument);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_web_page_get_message_count();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_web_page_take_message();
    }

}
